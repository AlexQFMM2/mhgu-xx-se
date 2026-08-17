#include "main_window.hpp"

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>

static void applyApplicationStyle(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, QColor("#e6ebf2"));
    palette.setColor(QPalette::WindowText, QColor("#111827"));
    palette.setColor(QPalette::Base, QColor("#f9fbfd"));
    palette.setColor(QPalette::AlternateBase, QColor("#edf2f7"));
    palette.setColor(QPalette::Text, QColor("#111827"));
    palette.setColor(QPalette::Button, QColor("#e8eef6"));
    palette.setColor(QPalette::ButtonText, QColor("#111827"));
    palette.setColor(QPalette::Highlight, QColor("#2563ad"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor("#7d8999"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#7d8999"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#7d8999"));
    app.setPalette(palette);
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setStyleHint(QFont::SansSerif);
    font.setPointSize(10);
    app.setFont(font);
    app.setStyleSheet(QStringLiteral(R"STYLE(
        QWidget { background: #e6ebf2; color: #182033; }
        QLabel { background: transparent; }
        QCheckBox, QRadioButton { color: #243247; background: transparent; spacing: 7px; }
        QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; }
        QLabel#appTitle { color: #15213a; font-size: 24px; font-weight: 700; }
        QLabel#appSubtitle { color: #69758a; font-size: 12px; }
        QMainWindow, QWidget#mainSurface { background: #e6ebf2; }
        QFrame#sidebar { background: #18243a; border: 0; }
        QLabel#sidebarTitle { color: #ffffff; font-size: 20px; font-weight: 700; }
        QLabel#sidebarCaption { color: #9eacc1; font-size: 12px; }
        QLabel#pageTitle { color: #15213a; font-size: 22px; font-weight: 700; }
        QLabel#emptyTitle { color: #344158; font-size: 20px; font-weight: 600; }
        QLabel#riskWarning { color: #8a4b08; background: #fff7df; border: 1px solid #eccb78; border-radius: 8px; padding: 7px 11px; }
        QWidget#pageSurface { background: #f8fafc; }
        QLabel#statusLabel { color: #6e788a; background: #eef2f7; border: 1px solid #dce3ed; border-radius: 8px; padding: 9px 12px; }
        QLabel#statusLabel[loaded="true"] { color: #17643a; background: #eaf8f0; border-color: #bce6cd; }
        QLabel#statusLabel[dirty="true"] { color: #8a4b08; background: #fff7df; border-color: #eccb78; }
        QLabel#warningLabel { color: #8a4b08; background: #fff7df; border: 1px solid #eccb78; border-radius: 8px; padding: 9px 12px; }
        QFrame#contentCard, QFrame#slotCard { background: #fbfcfe; border: 1px solid #b9c5d3; border-radius: 12px; }
        QFrame#footerBar { background: #f8fafc; border: 1px solid #b9c5d3; border-radius: 10px; }
        QScrollArea#contentArea, QScrollArea#contentArea > QWidget > QWidget { background: #f8fafc; border-radius: 10px; }
        QPushButton { color: #26344c; background: #f8fafc; border: 1px solid #a8b6c8; border-radius: 8px; padding: 8px 14px; min-height: 24px; font-weight: 500; }
        QPushButton:hover { color: #1858a8; background: #f4f8ff; border-color: #7fa9e3; }
        QPushButton:pressed { background: #e8f1ff; border-color: #4f88d3; }
        QPushButton:disabled { color: #9aa4b4; background: #f0f3f7; border-color: #e0e5ec; }
        QPushButton#navigationButton { text-align: left; padding: 11px 15px; min-height: 32px; font-weight: 600; color: #cbd5e4; background: transparent; border-color: transparent; }
        QPushButton#navigationButton:hover { color: #ffffff; background: #24344f; border-color: #334765; }
        QPushButton#navigationButton:checked { color: #ffffff; background: #2769bd; border-color: #2769bd; }
        QPushButton#navigationButton:disabled { color: #627089; background: transparent; border-color: transparent; }
        QPushButton#primaryButton { color: #ffffff; background: #2769bd; border-color: #2769bd; font-weight: 700; }
        QPushButton#primaryButton:hover { background: #1f5ca9; border-color: #1f5ca9; }
        QPushButton#saveButton { color: #ffffff; background: #23845a; border-color: #23845a; font-weight: 700; }
        QPushButton#saveButton:hover { background: #1b714c; border-color: #1b714c; }
        QLineEdit, QSpinBox, QComboBox, QTextEdit, QPlainTextEdit { color: #1e293b; background: #fbfdff; border: 1px solid #91a1b6; border-radius: 7px; padding: 5px 8px; min-height: 22px; selection-background-color: #2d70c7; }
        QLineEdit:hover, QSpinBox:hover, QComboBox:hover, QTextEdit:hover, QPlainTextEdit:hover { border-color: #667b96; }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QTextEdit:focus, QPlainTextEdit:focus { border: 1px solid #2769bd; }
        QComboBox QAbstractItemView { color: #172033; background: #fbfdff; border: 1px solid #7f90a6; selection-background-color: #2769bd; selection-color: #ffffff; outline: 0; }
        QTableWidget, QTableView { color: #1e293b; background: #fbfdff; alternate-background-color: #edf2f7; border: 1px solid #9facbd; border-radius: 8px; gridline-color: #d0d8e3; selection-background-color: #2d70c7; selection-color: #ffffff; }
        QHeaderView::section { color: #344158; background: #dfe7f0; border: 0; border-right: 1px solid #b3c0d1; border-bottom: 1px solid #b3c0d1; padding: 7px 8px; font-weight: 600; }
        QGroupBox { background: #f8fafc; border: 1px solid #b3c0d1; border-radius: 9px; margin-top: 12px; padding-top: 9px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; background: #eef3f8; }
        QTabWidget::pane { border: 1px solid #b3c0d1; background: #f8fafc; border-radius: 8px; }
        QTabBar::tab { background: #dfe7f0; border: 1px solid #b3c0d1; padding: 8px 14px; }
        QTabBar::tab:selected { color: #1858a8; background: #fbfdff; }
        QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
        QScrollBar::handle:vertical { background: #b8c3d2; min-height: 28px; border-radius: 5px; }
        QToolTip { color: #ffffff; background: #26344c; border: 0; padding: 5px 7px; }
    )STYLE"));
}

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MHGU / MHXX Save Editor"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    applyApplicationStyle(app);
    MainWindow window;
    window.show();
    if (app.arguments().contains(QStringLiteral("--smoke-test")))
        QTimer::singleShot(150, &app, &QCoreApplication::quit);
    return app.exec();
}
