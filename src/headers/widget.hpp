#pragma once

#include <QWidget>

// Community and support cards (Discord, Ko-fi). Shown in the help dialog.
QWidget *widget_create_kofi_card(QWidget *parent);
QWidget *widget_create_discord_card(QWidget *parent);

// Opens the help dialog (quick start, community, resources). Used by the dock info button.
void show_troubleshooting_dialog(QWidget *parent);
