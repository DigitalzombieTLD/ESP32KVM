#pragma once
#include "term.h"

enum ShellResult { SHELL_CONTINUE, SHELL_EXIT };

void shellBanner(Term &t);
ShellResult shellExecute(Term &t, const String &line);
// Interactive loop; returns when the user exits or the connection closes.
void shellRun(Term &t);
