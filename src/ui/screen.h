#pragma once

void screenInit();
// repaints only the regions whose content changed since the last call —
// cheap enough to run every loop pass
void screenRender();
// full-screen two-line notice (Wi-Fi portal etc.); screenRender() repaints
// the ticker after screenInvalidate()
void screenMessage(const char* line1, const char* line2);
void screenInvalidate();
