#pragma once

#include "mixer_state.h"

void x32ClientInit();
void x32ClientTick();
void x32ClientDiscover();
void x32ClientConnect(const char *ip);
void x32ClientStartFullSync();
void x32ClientRequestBusSends(int busIdx);
void x32ClientProcessCommand(const AppCommand &cmd);
void x32ClientOnWifiLost();
void x32ClientOnWifiConnected();
