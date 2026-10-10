# IR Engine Text Parser

This C++ project reads documents, cleans their text, and creates word and document dictionaries.

The parser separates TREC documents, removes numbers and words containing digits, splits text at punctuation, converts words to lowercase, removes stopwords, and applies the supplied stemmer.

This guide explains how to build and run the project after receiving its source code in a ZIP file.

## Programs included

The `.cpp` files contain source code. CMake builds them into the executable programs listed below.

| Source file | Executable | Normal behavior |
|---|---|---|
| `main.cpp` | `IREngine` | Processes documents in `resources/corpus` |
| `parser_tests.cpp` | `ParserTests` | Processes the same bundled corpus |
| `input_main.cpp` | `InputParser` | Opens a graphical chooser for a data file |
| `application_tests.cpp` | `ApplicationTests` | Opens a graphical chooser for a data file |

**Start by running `IREngine` or `ParserTests` with no program arguments.**

Both use `resources/stoplist.txt` to remove stopwords. The stoplist is a filtering resource, not the document input.

Adding `--self-test` to `ParserTests` runs automated checks instead of processing the bundled corpus.

Follow the instructions to run the projects

## Supported systems

| Editor | Windows | macOS | Linux |
|---|---|---|---|
| VS Code | Yes | Yes | Yes |
| CLion | Yes | Yes | Yes |
| Xcode | No | Yes | No |

Xcode is available only on macOS. Use VS Code or CLion on Windows and Linux.

Use a 64-bit C/C++ compiler with C++17 support. Build the project separately on each operating system.

## 1. Prepare the extracted project

1. Extract the entire ZIP into a writable folder.
2. Open the folder that directly contains `CMakeLists.txt`. This is the **project root**.
3. Keep the source, resources, and vendor folders together.
4. Use a fresh build folder on your computer. Do not reuse build files created on another computer.

Check that these items are included:

| Item | Purpose |
|---|---|
| `CMakeLists.txt` | Defines the project and executable targets |
| `main.cpp` | Starts `IREngine` |
| `parser_tests.cpp` | Starts `ParserTests` |
| `input_main.cpp` | Starts `InputParser` |
| `application_tests.cpp` | Starts `ApplicationTests` |
| `src/` | Contains the shared parser source and headers |
| `resources/stoplist.txt` | Contains the stopwords |
| `resources/corpus/sample.trec` | Provides the bundled sample input |
| `vendor/stemmer.h` | Contains the supplied stemmer |

The four entry files may be in the project root or `src/`. Keep only one copy of each entry file because CMake checks both locations.

If the sample corpus is missing, restore it before running `IREngine` or `ParserTests` normally. The file-chooser programs can process a selected file without the bundled sample.

## 2. Check the required tools

This guide assumes your editor and C/C++ compiler are installed.

You also need:

- **CMake 3.20 or newer**
- **Git**
- **Ninja** for the VS Code configuration below
- **Zenity or KDialog on Linux** for graphical file selection

CTest is included with CMake. CLion can use its bundled CMake and Ninja.

The first configuration needs internet access. CMake downloads nlohmann/json, SQLite, miniz, Expat, and Portable File Dialogs automatically. Do not install or link these libraries manually.

### Windows

Install missing build tools in PowerShell:

```powershell
winget install --id Kitware.CMake --exact
winget install --id Git.Git --exact
winget install --id Ninja-build.Ninja --exact
```

Restart the terminal and editor after installation.

For MSVC, your Visual Studio installation must include **Desktop development with C++** and a Windows SDK.

When using MSVC with VS Code, open the **x64 Native Tools Command Prompt**. Change to the project root and start VS Code:

```bat
code .
```

If you use MinGW-w64, make sure its `bin` folder is on `PATH`. Its runtime DLLs must remain available when running the programs.

Windows provides the native file chooser.

### macOS

Open Xcode once and complete its first-run setup.

If Homebrew is installed, install missing tools:

```sh
brew install cmake ninja git
```

If Homebrew is missing, follow https://brew.sh and complete the instructions for adding it to `PATH`.

Check the selected Xcode installation:

```sh
xcodebuild -version
```

For an Xcode build, this command must identify the full Xcode installation.

If it reports that only Command Line Tools are selected, select the full Xcode installation. For the usual installation location:

```sh
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
```

Use the actual installation location if yours is different.

Use Apple Clang for your Mac’s native architecture. Ninja is optional when using Xcode instead of VS Code.

### Linux

On Ubuntu or Debian:

```sh
sudo apt update
sudo apt install cmake ninja-build git zenity debianutils
```

On Fedora:

```sh
sudo dnf install cmake ninja-build git zenity which
```

Use your installed GCC or Clang C/C++ compiler.

For another distribution, install the matching packages through its package manager. The dialog library also needs the `which` command to find desktop helpers.

The file-chooser programs require a graphical desktop. Normal `IREngine` and `ParserTests` runs do not require one.

### Verify the installation

Run these commands in a new terminal:

```sh
cmake --version
git --version
ctest --version
```

For VS Code, also check:

```sh
ninja --version
```

Each command should print a version. If a command is missing, check its installation and `PATH`.

CLion’s bundled tools may work inside CLion without being available in a separate terminal.

## 3. Build and run in VS Code

These instructions apply to Windows, macOS, and Linux.

### Configure and build

1. Select **File → Open Folder** and open the project root.
2. Install Microsoft’s **C/C++** and **CMake Tools** extensions.
3. In Workspace Settings, set:

   | Setting | Value |
      |---|---|
   | `cmake.buildDirectory` | `${workspaceFolder}/build-vscode` |
   | `cmake.generator` | `Ninja` |
   | `cmake.useCMakePresets` | `never` |

4. Open the Command Palette:
    - Windows/Linux: `Ctrl+Shift+P`
    - macOS: `Command+Shift+P`
5. Run **CMake: Select a Kit** and select your installed 64-bit compiler.
6. Run **CMake: Select Variant** and select **Debug**.
7. Run **CMake: Configure** and wait for it to finish.
8. Run **CMake: Set Build Target**, select **all**, then run **CMake: Build**.

Wait for a successful build before running a program.

### Run main.cpp as IREngine

Open **Terminal → New Terminal**. The terminal must be in the project root containing `CMakeLists.txt`.

On Windows:

```powershell
.\build-vscode\IREngine.exe
```

On macOS or Linux:

```sh
./build-vscode/IREngine
```

The program reads `resources/corpus` without opening a chooser.

### Run parser_tests.cpp as ParserTests

From the same project-root terminal:

On Windows:

```powershell
.\build-vscode\ParserTests.exe
```

On macOS or Linux:

```sh
./build-vscode/ParserTests
```

With no arguments, this performs the default parsing run.

To run automated parser checks instead:

On Windows:

```powershell
.\build-vscode\ParserTests.exe --self-test
```

On macOS or Linux:

```sh
./build-vscode/ParserTests --self-test
```

A successful automated run prints a message beginning with `[PASS]`.

You can also select `IREngine` or `ParserTests` under **CMake → Project Status → Launch** and click the play button. The terminal commands above make the working directory explicit.

Do not use a single-file “Run Code” action. This project must be built through CMake so all source files and libraries are included.

## 4. Build and run in CLion

These instructions apply to Windows, macOS, and Linux.

### Configure the project

1. Select **File → Open** and open the project root.
2. Open **Settings → Build, Execution, Deployment → Toolchains**.
3. Select your local toolchain:

   | System | Toolchain |
      |---|---|
   | Windows | Bundled MinGW, MinGW-w64, or Visual Studio/MSVC |
   | macOS | Apple Clang |
   | Linux | GCC or Clang |

4. Confirm that CLion detects the C compiler, C++ compiler, CMake, and build tool.
5. Under **Settings → Build, Execution, Deployment → CMake**, use:

   | Setting | Value |
      |---|---|
   | Build type | `Debug` |
   | Toolchain | Your selected local toolchain |
   | Generator | `Ninja` |
   | Build directory | `cmake-build-local` |

6. Apply the settings and reload the CMake project.
7. Wait for configuration and dependency downloads to finish.

On macOS, Settings is available through the CLion menu.

### Run main.cpp as IREngine

1. Open **Run → Edit Configurations**.
2. Select the **IREngine** CMake Application configuration.
3. Confirm that Target and Executable are `IREngine`.
4. Set Working directory to `$PROJECT_DIR$`.
5. Leave Program arguments empty.
6. Keep Build in the Before launch list.
7. Apply the settings and click **Run**.

CLion builds the program before starting it. Messages appear in the Run window.

### Run parser_tests.cpp as ParserTests

Select the **ParserTests** CMake Application configuration and use:

| Field | Value |
|---|---|
| Target | `ParserTests` |
| Executable | `ParserTests` |
| Working directory | `$PROJECT_DIR$` |
| Program arguments | Empty |

Click **Run** to process the bundled corpus.

For automated parser checks, enter this in Program arguments:

```text
--self-test
```

Remove the argument to return to normal parsing.

If either configuration is missing, add a **CMake Application** configuration and select the matching target.

## 5. Build and run in Xcode

Xcode instructions apply only to macOS.

### Generate and build the project

Open Terminal in the original project root.

Run each command after the previous command succeeds:

```sh
cmake -S . -B build-xcode -G Xcode
cmake --build build-xcode --config Debug --target IREngine ParserTests InputParser ApplicationTests
cmake --open build-xcode
```

These commands generate the Xcode project, build all four programs, and open the generated project.

### Run main.cpp as IREngine

1. Select the **IREngine** scheme.
2. Select **My Mac** as the destination.
3. Open **Product → Scheme → Edit Scheme**.
4. Under **Run → Arguments**, leave launch arguments empty.
5. Under **Run → Options**, enable **Use custom working directory** and select the original project root containing `CMakeLists.txt`.
6. Close the scheme editor and select **Product → Run**.

Messages appear in Xcode’s debug console.

### Run parser_tests.cpp as ParserTests

Select the **ParserTests** scheme and use the same setup:

- Destination: **My Mac**
- Working directory: the original project root
- Launch arguments: empty

Select **Product → Run** for a normal parsing run.

For automated parser checks, add `--self-test` under **Run → Arguments**. Remove it to return to normal parsing.

Choose an application scheme when running. `ALL_BUILD` and `ZERO_CHECK` are build targets, not parser programs.

### Run from Terminal

From the original project root:

```sh
./build-xcode/Debug/IREngine
./build-xcode/Debug/ParserTests
```

For automated parser checks:

```sh
./build-xcode/Debug/ParserTests --self-test
```

## 6. Check the results

With the working directory set to the project root, normal runs write:

```text
output/parser_output.txt
output/text_dictionary.txt
output/doc_dictionary.txt
```

| File | Contents |
|---|---|
| `text_dictionary.txt` | Final terms and their IDs |
| `doc_dictionary.txt` | Document names and their IDs |
| `parser_output.txt` | Both dictionaries and document token streams |

The supplied sample should report:

```text
[DONE] 2 documents; 7 terms; 8 tokens
```

This count applies to the bundled sample. Other input produces different counts.

Term IDs start at 1 and follow alphabetical order. Genuine FT911 document IDs match their numeric suffixes.

After initial validation, the application clears previous results before processing a new run. It does not append separate runs. If processing then fails, the result files are cleared.

All normal programs share the output files. Run one program at a time and copy any results you want to keep before starting another run.

Output paths are relative to the working directory. If an editor starts the program from a build folder, output appears inside that build folder. Follow the working-directory instructions above to keep results in the original project folder.

## 7. Run both automated test suites

Build `ParserTests` and `ApplicationTests` first.

For the VS Code build:

```sh
ctest --test-dir build-vscode --output-on-failure
```

For the CLion build, when system CTest is available:

```sh
ctest --test-dir cmake-build-local --output-on-failure
```

For the Xcode build:

```sh
ctest --test-dir build-xcode -C Debug --output-on-failure
```

The registered tests are `parser_tests` and `application_tests`. Both should pass.

Automated tests use temporary data and do not open a chooser or replace normal project output.

## 8. Common problems

| Problem | Fix |
|---|---|
| CMake cannot find the project | Open the folder directly containing `CMakeLists.txt`. |
| Build settings refer to another computer | Configure a fresh local build folder. |
| Compiler or generator mismatch | Use a new build folder after changing the compiler or generator. |
| Compiler not detected | Select the installed 64-bit compiler kit or toolchain. |
| A build tool is missing | Install it, check `PATH`, and restart the editor. |
| MSVC cannot find tools or headers | Check the C++ workload and Windows SDK; use the developer command prompt. |
| Dependency download fails | Check internet access and the reported error, then configure again. |
| Stoplist is missing | Restore `resources/stoplist.txt` with the exact spelling. |
| Default corpus is missing | Restore the sample or place the intended corpus in `resources/corpus`. |
| Output appears in another folder | Set the working directory to the original project root. |
| Output is locked | Stop other parser runs. After a crash, remove only the stale lock folder named in the error. |
| Linux chooser cannot open | Install Zenity or KDialog and run inside a graphical desktop. |
| CTest cannot find an executable | Build both test programs first. |
| CLion warns that an output file is large | This concerns editor features; check the program’s exit status separately. |

## 9. Additional functionality I added

I added `InputParser` and `ApplicationTests` to test the parser with other local datasets. Both open a graphical chooser, so I select the input file without entering its path in the console.

To run them, select the matching target in CLion or scheme in Xcode, use the project-root working directory, and leave arguments empty.

After a VS Code build, run one of these commands from the project root.

Windows:

```powershell
.\build-vscode\InputParser.exe
.\build-vscode\ApplicationTests.exe
```

macOS or Linux:

```sh
./build-vscode/InputParser
./build-vscode/ApplicationTests
```

After an Xcode build:

```sh
./build-xcode/Debug/InputParser
./build-xcode/Debug/ApplicationTests
```

I select one supported data file and click Open. Canceling leaves existing results unchanged. File selection stays local; the application does not send the data to a website.

Supported inputs are TXT, TREC/SGML, CSV, TSV, JSON, JSONL/NDJSON, and XLSX. Plain TXT is read by paragraph by default.

With the upload fix applied, unnamed records receive names such as `DOC#1` and IDs in input order. Genuine FT911 names keep their required IDs.

I use `--progress` for shorter console output and `--help` to view reading options. Adding `--self-test` to `ApplicationTests` runs automated checks instead of opening the chooser.