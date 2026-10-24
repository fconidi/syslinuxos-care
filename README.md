# SysLinuxOS Care

SysLinuxOS Care is a desktop utility for SysLinuxOS and compatible Debian-based systems. It brings resource monitoring and common maintenance tasks into one application. The current amd64 package targets SysLinuxOS 13 and Debian 13 (Trixie).
On SysLinuxOS, open it from **SysLinuxOS Tools → SysLinuxOS Care**.

## Install and launch

Install the downloaded package on the target system with `sudo apt install ./syslinuxos-care_0.1.1_amd64.deb`. Then open **SysLinuxOS Tools → SysLinuxOS Care**, or run `syslinuxos-care` from a terminal. System maintenance actions display a desktop authorization prompt; a polkit authentication agent must be running in the session. The package recommends MATE's agent on SysLinuxOS.

## Use the tabs

- **Overview:** See current CPU, memory, disk and network use. The chart labels CPU and memory history and marks percentages. Pause or resume monitoring, refresh immediately, or change the interval.
- **Cleanup:** Choose a category, scan, review the paths and sizes, select individual files or use **Select all**, then choose **Remove selected** and confirm. Browser cache and profile categories require the browser to be closed. APT archive removal also asks for authorization.
- **Trash:** Scan the home and supported mounted-volume trash, check each item's original location, select items individually or use **Select all**, then choose **Permanently remove selected** and confirm. This does not move items to another trash.
- **Disk space:** Choose a folder and analyze it. The table groups data by the first folder level; click the **Data size** heading to change the sort order. The analysis does not delete files.
- **Startup:** Refresh the application list, select one entry, then enable or disable its session startup. A user override can be reversed from the same tab.
- **Processes:** Search or sort the list, select a process owned by your user, then request termination and confirm. Refresh the list if the process has changed or disappeared.
- **Services:** Choose user or system services and refresh the list. Select a service to start or stop it, or change whether it starts automatically. System startup changes require authorization; changing startup does not start or stop the service now.
- **Packages:** Search the installed package list, select one package and choose **Preview removal**. Read the full APT plan before choosing **Remove reviewed package** and confirming the authorization prompt. Refresh and preview again if the package or plan changes.
- **Settings:** Choose the interface language and monitoring interval. A language change takes effect on the next launch.

Scans can be cancelled. A cancelled or changed scan must be run again before removal. Nothing is selected automatically for cleanup or trash removal.

## Data removal and recovery

File cleanup and emptying trash permanently remove the selected items. Close Firefox, Chromium or Chrome before scanning or changing their profiles. Removing cookies can sign you out; removing Chromium or Chrome History also removes its download list. Firefox history is not offered because it shares a database with bookmarks. The application does not create a backup or promise to erase every trace of browsing. Check the exact paths and the confirmation dialog before proceeding.

If an action is unavailable, read its status message, refresh the relevant list and check that the required browser, service manager or desktop authorization agent is available. An APT removal plan that has changed must be previewed again.

## Supported data and safeguards

Cleanup covers thumbnail and font caches, completed APT package archives, and discovered Firefox, Chromium and Google Chrome caches in standard local profiles. Browser cookie cleanup is separate from cache cleanup; Chromium and Chrome History are separate selections. APT archive cleanup shows individual `.deb` files, requires authorization and leaves in-progress downloads alone. Removed archives may need to be downloaded again.

The application asks for confirmation before removing files, changing startup, process or service state, or removing a package. It rechecks the selected state before changing startup entries, services or packages. If a reviewed APT plan changes, removal is refused. Cache, trash, service and package work runs without blocking the window.

Mounted-volume trash discovery follows the FreeDesktop `.Trash/<uid>` and `.Trash-<uid>` locations. Inaccessible volumes and unsupported remote trash services are not shown.
Trash folders are removable only after a complete scan. If their contents change before removal, the application stops and asks for a fresh scan.
Password databases and bookmarks are not selected for browser profile cleanup.

The interface follows the system language in English, Italian, French, German and Spanish. Settings can select a language for the next launch and retain the chosen monitoring interval.

Copyright 2026 Franco Conidi. Licensed under GPL-3.0-only; see LICENSE.
