# SysLinuxOS Care

SysLinuxOS Care is a desktop utility for SysLinuxOS and compatible Debian-based systems. It brings resource monitoring and common maintenance tasks into one application.

The current version provides:

- CPU, memory, disk and network overview.
- Preview and selective removal of thumbnail and font caches.
- Disk usage grouped by the first folder level.
- Session startup entries with reversible user overrides.
- Process listing and confirmed termination of processes owned by the current user.
- System service overview and controls for user services.
- Installed package inventory and APT removal preview.

Cache files are never selected for removal automatically. The application asks for confirmation before removing files or changing startup, process or service state. Package removal is currently a preview only.

Some functions depend on the desktop session and installed system tools. If a function is unavailable, the application displays the error from the underlying service.
