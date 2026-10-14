#pragma once

#include <QString>
#include <QStringList>

namespace tracelab {

// Opening a block's file in another editor. TraceLab watches the file, so a
// save there compiles it here (LabControllerDevice, CodePlantDevice).
namespace external {

// VS Code's executable on this machine (Code.exe on Windows, the `code`
// launcher elsewhere); empty when it is not found.
QString findVsCode();

// The arguments that open `path` at `line` (1-based; 0: wherever VS Code
// left it) in VS Code, with `folder` as the window's folder when the file
// is inside it -- the folder holding the editor configuration
// (CodeLibrary::writeEditorConfig()).
QStringList vsCodeArguments(const QString& path, int line, const QString& folder);

// Starts VS Code (`program`, from findVsCode()); false when it cannot start.
bool openInVsCode(const QString& program, const QString& path, int line, const QString& folder);

// The application the system associates with .c files; on Windows, the
// "Open with" dialog when there is none.
bool openWithDefaultApp(const QString& path);

// The system's dialog to choose the program (Windows only; false
// elsewhere).
bool openWithChooser(const QString& path);
bool hasChooser();

}  // namespace external

}  // namespace tracelab
