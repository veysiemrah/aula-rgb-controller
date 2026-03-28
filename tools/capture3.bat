@echo off
echo === Aula F87 Pro USB Capture 3 ===
echo === Handshake + Speed/Direction ===
echo.
del /f capture3.pcap 2>nul
echo Adimlar:
echo   1. Aula yazilimini KAPATILI olsun
echo   2. Yakalama basladiginda Aula yazilimini AC (5 sn bekle)
echo   3. Breathing efektini sec, hizi degistir (ornegin 1 ve sonra 4)
echo   4. Yonlu bir efekt sec (wave/marquee), yonu degistir
echo   5. Yazilimi kapat
echo   6. Ctrl+C ile durdur
echo.
echo Yakalama basliyor...
"C:\Program Files\USBPcap\USBPcapCMD.exe" -d \\.\USBPcap2 -A -o capture3.pcap --snaplen 65535
