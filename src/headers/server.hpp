#pragma once
#include <QString>

int  server_start(const QString &doc_root, int preferred_port);
void server_stop();
int  server_port();
bool server_is_running();
QString server_doc_root();