Project Overview

A portable file-sharing system built on NodeMCU ESP8266 that creates a wireless access point and serves a web interface for accessing digital documents and hosting discussions.

Hardware Requirements
NodeMCU ESP8266
Micro SD Card Module
Micro SD Card (Max 32GB)
Power source (USB power bank or similar)
Required wiring/jumpers

Wiring Configuration
NodeMCU -> SD Card Module
D5 (GPIO14) -> SCK
D6 (GPIO12) -> MISO
D7 (GPIO13) -> MOSI
D8 (GPIO15) -> CS
3.3V -> VCC
GND -> GND

Features

Wireless Access Point
No password required
Auto-redirects through captive portal
Custom landing page

File Management
Supports PDF, EPUB, DOC, RTF, TXT formats
Files organized alphabetically
Collapsible file groups by first letter
File count per letter group

Allow uploading files with supported formats

Forum System
Thread creation and management
Post creation within threads
1-hour auto-cleanup of forum content
Automatic thread refresh

