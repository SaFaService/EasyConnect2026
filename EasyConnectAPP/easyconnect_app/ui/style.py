APP_STYLE = """
QWidget {
  background-color: #eef4fb;
  color: #1c2430;
  font-family: "Segoe UI";
  font-size: 12px;
}
QLabel {
  background: transparent;
}
QMainWindow {
  background-color: #eef4fb;
}
QGroupBox {
  border: 1px solid #d2dbe4;
  border-radius: 14px;
  margin-top: 12px;
  padding: 14px;
  background-color: #ffffff;
  font-weight: 600;
}
QGroupBox::title {
  subcontrol-origin: margin;
  left: 12px;
  padding: 0 4px;
}
#LoginCard {
  background-color: rgba(255, 255, 255, 0.94);
  border: 1px solid #c5d8ea;
  border-radius: 20px;
  margin-top: 0;
}
#LoginTitle {
  font-size: 24px;
  font-weight: 700;
  color: #1c2430;
  qproperty-alignment: AlignCenter;
}
#LoginFieldLabel {
  font-size: 13px;
  font-weight: 700;
  color: #1c2430;
}
#LoginStatus {
  color: #9a1f1f;
  font-size: 12px;
  background: transparent;
  border: none;
  padding: 0;
}
#ForgotBtn {
  color: #1f5d8c;
  background: transparent;
  border: 0;
  padding: 0;
  text-decoration: underline;
}
#ForgotBtn:hover {
  color: #133d5c;
}
QPushButton {
  border: 0;
  border-radius: 8px;
  background-color: #14548a;
  color: #ffffff;
  padding: 8px 14px;
  font-weight: 600;
}
QPushButton:hover {
  background-color: #0f426c;
}
QPushButton:disabled {
  background-color: #97a8b8;
}
#HomeTitle {
  font-size: 24px;
  font-weight: 700;
  color: #123f5f;
}
#HomeSession {
  color: #294257;
  font-weight: 600;
  font-size: 13px;
}
#HomeSessionLink {
  color: #294257;
  font-weight: 700;
  font-size: 13px;
  border: 0;
  background: transparent;
  text-align: right;
  padding: 0;
}
#HomeSessionLink:hover {
  color: #123f5f;
  text-decoration: underline;
}
#SerialLinkBtn {
  color: #14548a;
  background: transparent;
  border: 0;
  text-align: left;
  padding: 0;
  font-weight: 700;
}
#SerialLinkBtn:hover {
  color: #0f426c;
  text-decoration: underline;
}
#PlantsTitle {
  font-size: 24px;
  font-weight: 700;
  color: #123f5f;
}
#PlantsStatus {
  color: #294257;
  font-weight: 600;
  font-size: 12px;
}
QToolButton#HomeTile {
  background-color: #ffffff;
  color: #15324a;
  border: 1px solid #cde0f0;
  border-radius: 12px;
  text-align: center;
  padding: 8px;
  font-size: 13px;
  font-weight: 700;
}
QToolButton#HomeTile:hover {
  background-color: #f7fbff;
  border: 1px solid #a8c9e2;
}
QToolButton#HomeTile:pressed {
  background-color: #e8f3fb;
  padding-top: 10px;
  padding-left: 10px;
}
QLineEdit, QComboBox, QSpinBox, QTableWidget, QPlainTextEdit, QTabWidget::pane {
  background-color: #ffffff;
  border: 1px solid #c7d1dd;
  border-radius: 8px;
  padding: 6px;
}
#RememberUsernameChk {
  font-weight: 600;
  color: #28455d;
}
#ComSelector {
  min-width: 220px;
}
QHeaderView::section {
  background-color: #edf3f8;
  border: 0;
  border-right: 1px solid #dce5ef;
  border-bottom: 1px solid #dce5ef;
  font-weight: 600;
  padding: 6px;
}
QStatusBar#AppStatusBar {
  background-color: #dfeaf5;
  border-top: 1px solid #c5d8ea;
}
QStatusBar#AppStatusBar QLabel {
  color: #1f3f5a;
  font-size: 11px;
  padding: 2px 8px;
}
"""
