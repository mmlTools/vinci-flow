#pragma once

#include <QWidget>

QWidget *widget_create_kofi_card(QWidget *parent);
QWidget *widget_create_discord_card(QWidget *parent);
QWidget *create_widget_carousel(QWidget *parent);

// Opens a richer help dialog (used by the dock info button).
void show_troubleshooting_dialog(QWidget *parent);
