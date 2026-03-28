@echo off
echo === Aula F87 Pro USB Capture ===
echo.
del /f capture.pcap 2>nul
echo Tum USB cihazlari yakalanacak (USBPcap2)...
echo Durdurmak icin Ctrl+C basin.
echo.
"C:\Program Files\USBPcap\USBPcapCMD.exe" -d \\.\USBPcap2 -A -o capture.pcap --snaplen 65535
