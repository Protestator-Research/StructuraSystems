# StructuraSystems

## Motivation

This system is a test platform for the SysMLv2 Library we are currently developing. Also the digital twin development shoould be supported to have a seperation between the individual software.

This Software part will be further extended with additional features. Stay tuned!

## Features

The desktop app is a Qt Quick / Material Design application (light, dark or system theme).

- **Explorer**: projects of the working folder (`.md`, `.sysml`, `.kerml`, ...) with a live filter; double-click or Enter opens a project.
- **Online**: connect to a SysML v2 server, browse, open, commit and create online projects; upload local projects.
- **Digital twins**: wizard that creates a digital twin from selected elements of an online project.
- **Card editor**: documents are lists of element cards (Markdown, YAML, SysMLv2, KerML) with syntax highlighting, inline editing, reordering, insertion and deletion.
- **Problems**: F5 parses the current document; findings are listed in a resizable sheet, click one to jump to its card. Long messages are shown in a tooltip and can be copied via the context menu.
- **Quit protection**: unsaved documents are listed before the application closes.
- Window size, position, side panel and last destination are remembered between sessions.

| Shortcut | Action |
| --- | --- |
| Ctrl+O | Open folder |
| Ctrl+Shift+O | Open files |
| Ctrl+N | New project |
| Ctrl+S | Save current document (local files) |
| F5 | Parse and check current document |
| Ctrl+W | Close current document |
| Ctrl+, | Settings |
| Ctrl+Q | Quit |
| Ctrl+Enter / Esc | Apply / cancel editing of a card |

## Licensing

This software is licensed under the GPL v3, allowing a free and open source editing of the code. We want to offer you the option to use this libary also for commercial project. This will be done in a later step, since we are currelty developing this from the ground up.

## Build

Dependencies are managed with [Conan 2](https://conan.io/); a C++20 compiler and CMake are required.

```
git clone <repository-url> && cd StructuraSystems
git submodule update --init            # icons, see "Icons" below
conan install . --build missing -s build_type=Debug
source build/Debug/generators/conanrun.sh   # Qt tools (qmlimportscanner, ...) need the Conan libraries
cmake --preset conan-debug
cmake --build --preset conan-debug
```

Notes:

- On Linux the recipe builds Qt (6.11) from source with `qtdeclarative` and `qtshadertools`, which the Qt Quick UI needs. **The first build takes a long time**; later builds use the Conan cache.
- The generated environment (`source build/Debug/generators/conanrun.sh`) is required for configuring, building and running: the Qt host tools and the app are linked against shared Conan libraries (e.g. ICU).
- The sources are compiled with `-Wall -Wextra -Wpedantic -Werror`.

## Run

```
source build/Debug/generators/conanrun.sh
./build/Debug/out/StructuraSystems
```

On first start the working directory defaults to your documents folder; change it, the server address and the theme in the settings. Settings are stored with `QSettings` (organisation "Working Group Cyber Physical Systems", application "Structura Systems"), the working directory below `[WORKSPACE]`, window state below `[Window]`.

## Project structure

```
stucturasystems/
  src/main.cpp                     application entry, loads the QML module
  src/ViewModels/                  C++ view models exposed to QML
    AppController                  singleton: projects, open documents, server connection, background jobs
    DocumentModel                  elements of one document (cards)
    OpenDocumentsModel             open documents (tabs)
    ProblemListModel               parser findings
    ProjectFilterModel             filter proxy for the project lists
    SettingsController             settings as QML properties
    SysMLHighlighter               syntax highlighting for the editors
  src/Models/                      data models, settings storage, parsers (Markdown, SysML)
  src/Services/                    backend communication, digital twin entities
  qml/Main.qml                     window, shortcuts, layout
  qml/Theme.qml                    colours, metrics and icon locations (singleton)
  qml/components/                  reusable controls (navigation rail, lists, dialog footer, ...)
  qml/views/                       explorer, online and twin panes, editor, element cards, problems sheet
  qml/dialogs/                     settings, new project, commit, wizard, quit, about, ...
  resources/                       icons (git submodule)
```

## Icons

We are using the Icons of [flatart_icons](https://www.flaticon.com/de/autoren/flatart-icons) from flaticon.com. They make great art and please Support them. We are not allowed to share the Icons, thus we have them in an extra Resources Repository.