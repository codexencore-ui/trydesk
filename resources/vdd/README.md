# Bundled Virtual Display Driver (monitor virtual)

Berkas di folder ini **dipaketkan di installer TryDesk** agar remote tetap jalan ketika monitor
host mati/tidak ada. User tinggal install TryDesk lalu pakai — tidak ada langkah manual.

## Kenapa ini ada

Ketika tidak ada monitor aktif, driver GPU Windows berhenti menggambar desktop, sehingga capture
(DXGI Desktop Duplication, `src/native/trydesk-capture.cpp`) tidak dapat frame: gambar beku/hitam.
Solusinya memberi Windows sebuah **monitor virtual** (sama seperti TeamViewer). Ini hanya mungkin
lewat driver tampilan virtual; pendekatan lain (cegah-layar-mati, Windows Graphics Capture) tidak
menyelesaikannya.

## Apa yang dipaketkan

| Folder | Isi | Sumber |
| --- | --- | --- |
| `driver/` | Driver monitor virtual (IddCx/UMDF): `MttVDD.inf`, `MttVDD.dll`, `mttvdd.cat`, `vdd_settings.xml` | VirtualDrivers/Virtual-Display-Driver, rilis **25.7.23**, aset `VirtualDisplayDriver-x86.Driver.Only.zip` (x64) |
| `nefcon/` | Alat pemasang device driver tanpa jendela: `nefconw.exe`, `nefconc.exe` | nefarius/nefcon, rilis **v1.14.0** (x64) |
| `LICENSE-VDD.txt` | Lisensi MIT driver | — |
| `LICENSE-nefcon.txt` | Lisensi MIT nefcon | — |

Keduanya **MIT** dan mengizinkan distribusi ulang. Salinan lisensi disertakan.

## Cara pemasangan (dipakai oleh `trydesk-display.exe`)

1. Impor sertifikat dari `driver/mttvdd.cat` ke `Cert:\LocalMachine\TrustedPublisher`.
   Tanpa ini, instalasi gagal dengan "the publisher ... has not yet been established as trusted".
2. `nefcon/nefconw.exe install <path>\driver\MttVDD.inf "Root\MttVDD"`.

`pnputil` **tidak bisa** untuk driver ini (pnputil memasang paket driver, bukan device node);
karenanya nefcon dipakai. Lihat juga `Community Scripts/silent-install.ps1` di repo VDD, yang jadi
acuan resep ini.

## Catatan penting

- Butuh **Microsoft Visual C++ Redistributable**.
- Sertifikat ditandatangani lewat **SignPath**, jadi tidak perlu test signing di x64.
- Sebelum **update driver GPU/chipset mayor**, sebaiknya uninstall driver virtual ini dulu; bila
  layar bermasalah, boot **Safe Mode** lalu uninstall.
- Driver kernel bisa ditandai antivirus/EDR pada sebagian PC.
- Rilis ini untuk **x64**. ARM64 di Windows 11 24H2+ mungkin perlu test signing.

## Kredit

- Virtual Display Driver — https://github.com/VirtualDrivers/Virtual-Display-Driver (MIT)
- nefcon — https://github.com/nefarius/nefcon (MIT)

Jika Anda membangun ulang atau memperbarui versi, perbarui tabel di atas beserta `NOTICE`.
