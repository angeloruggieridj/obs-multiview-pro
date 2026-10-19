#pragma once

#include <QWidget>

/* Top-level, empty multiview window. Destroyed on close. */
class MultiviewWindow : public QWidget {
	Q_OBJECT

public:
	explicit MultiviewWindow(QWidget *parent = nullptr);
	~MultiviewWindow() override;
};
