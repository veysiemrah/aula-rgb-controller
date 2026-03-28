@echo off
echo === Tum Control OUT paketleri (ilk 32 byte) ===
for %%f in (9595 9603 9613 22481 22563 22877 29009 29013 29017 33745 33759 34009) do (
    echo.
    echo --- Frame %%f ---
    "C:\Program Files\Wireshark\tshark.exe" -r "%~dp0capture.pcap" -Y "frame.number==%%f" -T fields -e data.data 2>nul
)

echo.
echo === Tum Control IN paketleri ===
for %%f in (9611 9612 22629 22636 29015 29016 33783 33786) do (
    echo.
    echo --- Frame %%f ---
    "C:\Program Files\Wireshark\tshark.exe" -r "%~dp0capture.pcap" -Y "frame.number==%%f" -T fields -e data.data 2>nul
)
