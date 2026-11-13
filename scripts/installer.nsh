; TryDesk Setup (electron-builder NSIS include, see package.json build.nsis.include).
; Installs the TryDesk Windows service, as AnyDesk does: the PC stays reachable after a restart and while an RDP
; session is disconnected, under the same ID. One UAC prompt; declining it leaves TryDesk working without the service
; (Settings -> Security -> Install service does it later). An update reinstalls it, which refreshes the service's
; copy in Program Files; only a real uninstall removes it.
;
; The virtual-monitor helper is installed the same way: when the host has no active monitor, Windows stops drawing
; the desktop and the remote shows a frozen/black picture. Giving Windows a virtual monitor fixes it (the approach
; TeamViewer uses); TryDesk bundles the open-source Virtual Display Driver (resources\vdd, MIT). One more UAC prompt;
; declining leaves TryDesk working everywhere else (Settings -> Saat menjadi host -> "Pasang monitor virtual" does it
; later). An update refreshes it; only a real uninstall removes it.

!macro customInstall
  ExecShellWait "runas" "$INSTDIR\resources\app.asar.unpacked\src\native\trydesk-display.exe" 'install --dir "$INSTDIR\resources\vdd"' SW_HIDE
  ExecShellWait "runas" "$INSTDIR\resources\app.asar.unpacked\src\native\trydesk-service.exe" 'install "$INSTDIR\${APP_EXECUTABLE_FILENAME}"' SW_HIDE
!macroend

!macro customUnInstall
  ${ifNot} ${isUpdated}
    ExecShellWait "runas" "$INSTDIR\resources\app.asar.unpacked\src\native\trydesk-service.exe" "uninstall" SW_HIDE
    ExecShellWait "runas" "$INSTDIR\resources\app.asar.unpacked\src\native\trydesk-display.exe" 'remove --dir "$INSTDIR\resources\vdd"' SW_HIDE
  ${endIf}
!macroend
