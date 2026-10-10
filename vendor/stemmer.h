#ifndef IR_VENDOR_STEMMER_H
#define IR_VENDOR_STEMMER_H

/*
 * Adapted from Martin Porter's ANSI C Porter stemmer.
 * Porter, 1980: An algorithm for suffix stripping.
 */

#include <string.h>

#define TRUE 1
#define FALSE 0

static char* b;
static int k;
static int k0;
static int j;

// Decide whether a letter acts as a consonant in this word.
static int cons(int i)
{
    int invert = FALSE;

    while (i > k0 && b[i] == 'y') {
        invert = !invert;
        --i;
    }

    const int consonant = !(
        b[i] == 'a' ||
        b[i] == 'e' ||
        b[i] == 'i' ||
        b[i] == 'o' ||
        b[i] == 'u'
    );

    return invert ? !consonant : consonant;
}

// Count vowel and consonant groups used by the supplied stemming rules.
static int m()
{
    int count = 0;
    int i = k0;

    while (TRUE) {
        if (i > j) {
            return count;
        }

        if (!cons(i)) {
            break;
        }

        ++i;
    }

    ++i;

    while (TRUE) {
        while (TRUE) {
            if (i > j) {
                return count;
            }

            if (cons(i)) {
                break;
            }

            ++i;
        }

        ++i;
        ++count;

        while (TRUE) {
            if (i > j) {
                return count;
            }

            if (!cons(i)) {
                break;
            }

            ++i;
        }

        ++i;
    }
}

// Check whether the current stem contains at least one vowel.
static int vowelinstem()
{
    for (int i = k0; i <= j; ++i) {
        if (!cons(i)) {
            return TRUE;
        }
    }

    return FALSE;
}

// Check for matching consonants at the end of the current stem.
static int doublec(int position)
{
    if (position < k0 + 1) {
        return FALSE;
    }

    if (b[position] != b[position - 1]) {
        return FALSE;
    }

    return cons(position);
}

// Recognize the final consonant vowel consonant pattern used for short words.
static int cvc(int i)
{
    if (i < k0 + 2 ||
        !cons(i) ||
        cons(i - 1) ||
        !cons(i - 2)) {
        return FALSE;
    }

    const int ch = b[i];

    if (ch == 'w' || ch == 'x' || ch == 'y') {
        return FALSE;
    }

    return TRUE;
}

// Match a suffix whose first byte stores its character count.
static int ends(const char* suffix)
{
    const int length = suffix[0];

    if (suffix[length] != b[k]) {
        return FALSE;
    }

    if (length > k - k0 + 1) {
        return FALSE;
    }

    if (memcmp(
            b + k - length + 1,
            suffix + 1,
            length
        ) != 0) {
        return FALSE;
    }

    j = k - length;
    return TRUE;
}

// Replace the matched suffix and update the current word ending.
static void setto(const char* replacement)
{
    const int length = replacement[0];

    memmove(
        b + j + 1,
        replacement + 1,
        length
    );

    k = j + length;
}

// Apply a suffix replacement only when the stem measure allows it.
static void r(const char* replacement)
{
    if (m() > 0) {
        setto(replacement);
    }
}

// Handle common plural endings and simple past or ongoing verb endings.
static void step1ab()
{
    if (b[k] == 's') {
        if (ends("\04" "sses")) {
            k -= 2;
        } else if (ends("\03" "ies")) {
            setto("\01" "i");
        } else if (b[k - 1] != 's') {
            --k;
        }
    }

    if (ends("\03" "eed")) {
        if (m() > 0) {
            --k;
        }
    } else if (
        (ends("\02" "ed") || ends("\03" "ing")) &&
        vowelinstem()
    ) {
        k = j;

        if (ends("\02" "at")) {
            setto("\03" "ate");
        } else if (ends("\02" "bl")) {
            setto("\03" "ble");
        } else if (ends("\02" "iz")) {
            setto("\03" "ize");
        } else if (doublec(k)) {
            --k;

            const int ch = b[k];

            if (ch == 'l' || ch == 's' || ch == 'z') {
                ++k;
            }
        } else if (m() == 1 && cvc(k)) {
            setto("\01" "e");
        }
    }
}

// Change a final y when the earlier stem contains a vowel.
static void step1c()
{
    if (ends("\01" "y") && vowelinstem()) {
        b[k] = 'i';
    }
}

// Reduce longer suffixes using the supplied second group of stemming rules.
static void step2()
{
    switch (b[k - 1]) {
        case 'a':
            if (ends("\07" "ational")) {
                r("\03" "ate");
                break;
            }

            if (ends("\06" "tional")) {
                r("\04" "tion");
                break;
            }

            break;

        case 'c':
            if (ends("\04" "enci")) {
                r("\04" "ence");
                break;
            }

            if (ends("\04" "anci")) {
                r("\04" "ance");
                break;
            }

            break;

        case 'e':
            if (ends("\04" "izer")) {
                r("\03" "ize");
            }

            break;

        case 'l':
            if (ends("\03" "bli")) {
                r("\03" "ble");
                break;
            }

            if (ends("\04" "alli")) {
                r("\02" "al");
                break;
            }

            if (ends("\05" "entli")) {
                r("\03" "ent");
                break;
            }

            if (ends("\03" "eli")) {
                r("\01" "e");
                break;
            }

            if (ends("\05" "ousli")) {
                r("\03" "ous");
                break;
            }

            break;

        case 'o':
            if (ends("\07" "ization")) {
                r("\03" "ize");
                break;
            }

            if (ends("\05" "ation")) {
                r("\03" "ate");
                break;
            }

            if (ends("\04" "ator")) {
                r("\03" "ate");
                break;
            }

            break;

        case 's':
            if (ends("\05" "alism")) {
                r("\02" "al");
                break;
            }

            if (ends("\07" "iveness")) {
                r("\03" "ive");
                break;
            }

            if (ends("\07" "fulness")) {
                r("\03" "ful");
                break;
            }

            if (ends("\07" "ousness")) {
                r("\03" "ous");
                break;
            }

            break;

        case 't':
            if (ends("\05" "aliti")) {
                r("\02" "al");
                break;
            }

            if (ends("\05" "iviti")) {
                r("\03" "ive");
                break;
            }

            if (ends("\06" "biliti")) {
                r("\03" "ble");
                break;
            }

            break;

        case 'g':
            if (ends("\04" "logi")) {
                r("\03" "log");
            }

            break;
    }
}

// Remove or shorten suffixes using the supplied third rule group.
static void step3()
{
    switch (b[k]) {
        case 'e':
            if (ends("\05" "icate")) {
                r("\02" "ic");
                break;
            }

            if (ends("\05" "ative")) {
                r("\00" "");
                break;
            }

            if (ends("\05" "alize")) {
                r("\02" "al");
                break;
            }

            break;

        case 'i':
            if (ends("\05" "iciti")) {
                r("\02" "ic");
            }

            break;

        case 'l':
            if (ends("\04" "ical")) {
                r("\02" "ic");
                break;
            }

            if (ends("\03" "ful")) {
                r("\00" "");
                break;
            }

            break;

        case 's':
            if (ends("\04" "ness")) {
                r("\00" "");
            }

            break;
    }
}

// Remove selected endings when enough vowel and consonant groups remain.
static void step4()
{
    switch (b[k - 1]) {
        case 'a':
            if (ends("\02" "al")) {
                break;
            }

            return;

        case 'c':
            if (ends("\04" "ance")) {
                break;
            }

            if (ends("\04" "ence")) {
                break;
            }

            return;

        case 'e':
            if (ends("\02" "er")) {
                break;
            }

            return;

        case 'i':
            if (ends("\02" "ic")) {
                break;
            }

            return;

        case 'l':
            if (ends("\04" "able")) {
                break;
            }

            if (ends("\04" "ible")) {
                break;
            }

            return;

        case 'n':
            if (ends("\03" "ant")) {
                break;
            }

            if (ends("\05" "ement")) {
                break;
            }

            if (ends("\04" "ment")) {
                break;
            }

            if (ends("\03" "ent")) {
                break;
            }

            return;

        case 'o':
            if (ends("\03" "ion") &&
                j >= k0 &&
                (b[j] == 's' || b[j] == 't')) {
                break;
            }

            if (ends("\02" "ou")) {
                break;
            }

            return;

        case 's':
            if (ends("\03" "ism")) {
                break;
            }

            return;

        case 't':
            if (ends("\03" "ate")) {
                break;
            }

            if (ends("\03" "iti")) {
                break;
            }

            return;

        case 'u':
            if (ends("\03" "ous")) {
                break;
            }

            return;

        case 'v':
            if (ends("\03" "ive")) {
                break;
            }

            return;

        case 'z':
            if (ends("\03" "ize")) {
                break;
            }

            return;

        default:
            return;
    }

    if (m() > 1) {
        k = j;
    }
}

// Apply the final rules for trailing e and doubled l.
static void step5()
{
    j = k;

    if (b[k] == 'e') {
        const int measure = m();

        if (measure > 1 ||
            (measure == 1 && !cvc(k - 1))) {
            --k;
        }
    }

    if (b[k] == 'l' && doublec(k) && m() > 1) {
        --k;
    }
}

// Apply the supplied Porter steps and return the final character position.
inline int stem(char* word, int start, int end)
{
    b = word;
    k = end;
    k0 = start;

    if (k <= k0 + 1) {
        return k;
    }

    step1ab();
    step1c();
    step2();
    step3();
    step4();
    step5();

    return k;
}

#endif