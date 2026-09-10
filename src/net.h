#pragma once

// Starts the background networking task (pinned to core 0):
// Wi-Fi connect -> NTP sync -> repeated ESPN scoreboard fetches.
void net_start();
