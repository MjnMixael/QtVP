#pragma once

#include <QCoreApplication>
#include <QString>

#include <map>

class QWidget;

// Opens files extracted from a VP in another program: AnimStudio for animations,
// POF Tools for models, and the system's default app for everything else.
// One FileOpener covers one Open command, so a "not set up" question about a tool
// is asked once however many files need it.
class FileOpener
{
    Q_DECLARE_TR_FUNCTIONS(FileOpener)

public:
    enum class Tool { None, AnimStudio, PofTools };

    static Tool toolFor(const QString& fileName);
    static QString toolName(Tool tool);
    static QString toolPath(Tool tool);
    static void setToolPath(Tool tool, const QString& path);

    // File dialog filter for picking a program on this platform
    static QString programFilter();

    explicit FileOpener(QWidget* parent);

    // The file's tool if it has one, otherwise the system's default app for it
    void open(const QString& path);

    // Always lets the user pick the app
    void openWith(const QString& path);

private:
    enum class Choice { Unasked, Tool, System, Skip };

    Choice askAboutTool(Tool tool, const QString& fileName);
    bool launch(const QString& program, const QString& path);
    void openWithSystem(const QString& path);
    void chooseApplication(const QString& path);

    QWidget* m_parent;
    std::map<Tool, Choice> m_choices;
};
