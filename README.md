# SysLinuxOS Care

SysLinuxOS Care is a desktop utility for SysLinuxOS and compatible Debian-based systems. It brings resource monitoring and common maintenance tasks into one application. The current amd64 package targets SysLinuxOS 13 and Debian 13 (Trixie).

The current version provides:

- CPU, memory, disk and network overview, with CPU and memory history and adjustable refresh interval.
- Preview and selective removal of thumbnail and font caches.
- Preview and selective removal of discovered Firefox, Chromium and Google Chrome cache files when the browser is closed.
- Preview and selective permanent removal from the user's home trash and supported mounted-volume trash locations.
- Disk usage grouped by the first folder level.
- Session startup entries with reversible user overrides.
- Process listing and confirmed termination of processes owned by the current user.
- System service overview and controls for user services.
- Installed package inventory and APT removal preview.

Cache files are never selected for removal automatically. The application asks for confirmation before removing files or changing startup, process or service state. Package removal is currently a preview only.

Mounted-volume trash discovery follows the FreeDesktop `.Trash/<uid>` and `.Trash-<uid>` locations. Inaccessible volumes and unsupported remote trash services are not shown.
Browser cleanup currently targets cache files only. History, cookies, passwords and bookmarks are not changed.

Some functions depend on the desktop session and installed system tools. If a function is unavailable, the application displays the error from the underlying service.

The interface follows the system language in English, Italian, French, German and Spanish.

Copyright 2026 Franco Conidi. Licensed under GPL-3.0-only; see LICENSE.
