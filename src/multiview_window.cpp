#include "multiview_window.hpp"

#include <obs-module.h>

MultiviewWindow::MultiviewWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
	setAttribute(Qt::WA_DeleteOnClose);
	setWindowTitle(QStringLiteral("Multiview"));
	resize(960, 540);
	blog(LOG_INFO, "[obs-multiview-pro] MultiviewWindow created");
}

MultiviewWindow::~MultiviewWindow()
{
	blog(LOG_INFO, "[obs-multiview-pro] MultiviewWindow destroyed");
}
