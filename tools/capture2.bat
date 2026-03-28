@echo off
echo === Aula F87 Pro USB Capture 2 ===
echo.
del /f capture2.pcap 2>nul
echo Adimlar:
echo   1. Aula yazilimini ac
echo   2. Tum tuslara YESIL ata
echo   3. Tum tuslara BEYAZ ata
echo   4. LED'leri kapat (varsa)
echo   5. Ctrl+C ile durdur
echo.
echo Yakalama basliyor...
"C:\Program Files\USBPcap\USBPcapCMD.exe" -d \\.\USBPcap2 -A -o capture2.pcap --snaplen 65535
