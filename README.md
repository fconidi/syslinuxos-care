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
- System service overview, controls for user services and authorized changes to system service startup.
- Installed package inventory, APT removal preview and authorized package removal.

Cache files are never selected for removal automatically. The application asks for confirmation before removing files, changing startup, process or service state, or removing a package. APT removal requires polkit authorization. Immediately before starting APT, the helper checks the installed package version and removal plan again and refuses a changed plan.
Changing a system service startup setting also requires polkit authorization. The helper checks that the service is still in the reviewed state before changing it. Enabling or disabling startup does not start or stop a running service.

Mounted-volume trash discovery follows the FreeDesktop `.Trash/<uid>` and `.Trash-<uid>` locations. Inaccessible volumes and unsupported remote trash services are not shown.
Trash folders are removable only after a complete scan. If their contents change before removal, the application stops and asks for a fresh scan.
Browser cleanup currently targets cache files only. History, cookies, passwords and bookmarks are not changed.

Some functions depend on the desktop session and installed system tools. If a function is unavailable, the application displays the error from the underlying service.

The interface follows the system language in English, Italian, French, German and Spanish.

Copyright 2026 Franco Conidi. Licensed under GPL-3.0-only; see LICENSE.
