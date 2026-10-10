#pragma once

#include <nlohmann/json.hpp>
#include <array>
#include <functional>
#include <istream>
#include <set>
#include <stdexcept>

namespace ir {

class JsonRecords : public nlohmann::json_sax<nlohmann::json> {
    using Json = nlohmann::json;

    struct Frame {
        bool object;
        std::string path;
        std::string key;
        std::uint64_t index = 0;
        std::set<std::string> keys;
    };

    std::vector<Frame> frames_;
    std::vector<Json*> values_;
    Json record_;
    std::string target_;

    std::size_t arrayDepth_ = 0;
    std::size_t bytes_ = 0;
    std::size_t limit_;

    bool found_ = false;
    bool capturing_ = false;

    std::function<void(const Json&)> emit_;
    std::function<void()> reset_;

    static std::string escape(const std::string& value) {
        std::string result;

        for (char ch : value) {
            result += ch == '~'
                ? "~0"
                : ch == '/' ? "~1" : std::string(1, ch);
        }

        return result;
    }

    std::string nextPath() const {
        if (frames_.empty()) return {};

        const auto& parent = frames_.back();

        return parent.path + "/" +
            (parent.object
                ? escape(parent.key)
                : std::to_string(parent.index));
    }

    void startCapture(bool array) {
        const auto path = nextPath();

        if (!capturing_ &&
            arrayDepth_ &&
            frames_.size() == arrayDepth_) {
            capturing_ = true;
            bytes_ = 0;
        } else if (!found_ && path == target_) {
            found_ = true;

            if (array) {
                arrayDepth_ = frames_.size() + 1;
            } else {
                capturing_ = true;
                bytes_ = 0;
            }
        }
    }

    Json* put(Json value) {
        if (!capturing_) return nullptr;

        bytes_ += value.is_string()
            ? value.get_ref<const std::string&>().size() + 32
            : 32;

        if (bytes_ > limit_) {
            throw std::runtime_error("JSON record exceeds memory limit");
        }

        if (values_.empty()) {
            record_ = std::move(value);
            return &record_;
        }

        auto* parent = values_.back();

        if (parent->is_array()) {
            parent->push_back(std::move(value));
            return &parent->back();
        }

        return &((*parent)[frames_.back().key] = std::move(value));
    }

    void finishValue() {
        if (capturing_ && values_.empty()) {
            emit_(record_);
            record_ = nullptr;
            capturing_ = false;
            reset_();
        }

        if (!frames_.empty() && !frames_.back().object) {
            ++frames_.back().index;
        }
    }

    bool primitive(Json value) {
        startCapture(false);
        put(std::move(value));
        finishValue();
        return true;
    }

    bool start(bool object) {
        if (frames_.size() >= 64) {
            throw std::runtime_error("JSON nesting exceeds limit");
        }

        startCapture(!object);
        const auto path = nextPath();

        if (auto* value = put(object ? Json::object() : Json::array())) {
            values_.push_back(value);
        }

        frames_.push_back({object, path, {}, 0, {}});
        return true;
    }

    bool end() {
        if (capturing_) values_.pop_back();
        if (arrayDepth_ == frames_.size()) arrayDepth_ = 0;

        frames_.pop_back();
        finishValue();
        return true;
    }

public:
    JsonRecords(
        std::string target,
        std::size_t limit,
        std::function<void(const Json&)> emit,
        std::function<void()> reset
    )
        : target_(std::move(target)),
          limit_(limit),
          emit_(std::move(emit)),
          reset_(std::move(reset)) {
        Json::json_pointer validate(target_);
    }

    bool null() override {
        return primitive(nullptr);
    }

    bool boolean(bool value) override {
        return primitive(value);
    }

    bool number_integer(number_integer_t value) override {
        return primitive(value);
    }

    bool number_unsigned(number_unsigned_t value) override {
        return primitive(value);
    }

    bool number_float(number_float_t value, const string_t&) override {
        return primitive(value);
    }

    bool string(string_t& value) override {
        return primitive(value);
    }

    bool binary(binary_t&) override {
        throw std::runtime_error("Binary JSON is unsupported");
    }

    bool start_object(std::size_t) override {
        return start(true);
    }

    bool end_object() override {
        return end();
    }

    bool start_array(std::size_t) override {
        return start(false);
    }

    bool end_array() override {
        return end();
    }

    bool key(string_t& value) override {
        auto& frame = frames_.back();

        if (frame.keys.size() >= 4096 ||
            !frame.keys.insert(value).second) {
            throw std::runtime_error(
                "Duplicate JSON key or too many object fields"
            );
        }

        frame.key = value;

        if (capturing_) {
            bytes_ += value.size();

            if (bytes_ > limit_) {
                throw std::runtime_error(
                    "JSON record exceeds memory limit"
                );
            }
        }

        return true;
    }

    bool parse_error(
        std::size_t,
        const std::string&,
        const Json::exception& error
    ) override {
        throw std::runtime_error(error.what());
    }

    bool found() const {
        return found_;
    }
};

class JsonBuffer : public std::streambuf {
    std::streambuf* source_;
    std::array<char, 65536> buffer_{};
    std::size_t bytes_ = 0;
    std::size_t limit_;

protected:
    int_type underflow() override {
        const auto count = source_->sgetn(
            buffer_.data(), buffer_.size()
        );

        if (!count) return traits_type::eof();

        bytes_ += static_cast<std::size_t>(count);

        if (bytes_ > limit_ + buffer_.size()) {
            throw std::runtime_error(
                "JSON record or metadata exceeds byte limit"
            );
        }

        setg(
            buffer_.data(),
            buffer_.data(),
            buffer_.data() + count
        );

        return traits_type::to_int_type(*gptr());
    }

public:
    JsonBuffer(std::istream& source, std::size_t limit)
        : source_(source.rdbuf()), limit_(limit) {}

    void reset() {
        bytes_ = 0;
    }
};

inline void readJsonRecords(
    std::istream& input,
    const std::string& pointer,
    std::size_t limit,
    const std::function<void(const nlohmann::json&)>& handler
) {
    JsonBuffer buffer(input, limit);
    std::istream stream(&buffer);
    stream.exceptions(std::ios::badbit);

    JsonRecords sax(
        pointer,
        limit,
        handler,
        [&] { buffer.reset(); }
    );

    if (!nlohmann::json::sax_parse(stream, &sax) || !sax.found()) {
        throw std::runtime_error("JSON record location not found");
    }
}

}