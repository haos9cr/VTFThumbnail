VTFThumbnail {version} - thumbnails of Valve Texture Format (.vtf) files in Windows Explorer
https://github.com/haos9cr/VTFThumbnail

INSTALL
1. Put this folder where it will stay, e.g. C:\Program Files\VTFThumbnail\
   (Explorer loads the DLL from there - don't delete or move it afterwards).
2. Right-click install.cmd > Run as administrator  -> all users,
   or just double-click install.cmd                 -> only your account.
3. Open a folder with .vtf files (View > Large icons).

UNINSTALL
Run uninstall.cmd (as administrator if you installed for all users), then delete
the folder. If Windows says the DLL is in use, sign out and in again first.

ALPHA
Transparency is shown for textures with alpha, except normal / SSBump maps.
Change it in the registry, HKEY_CURRENT_USER\Software\VTFThumbnail, DWORD AlphaMode:
0 = auto (default), 1 = always transparency, 2 = always opaque.

-------------------------------------------------------------------------------

УСТАНОВКА
1. Положите эту папку туда, где она останется, например C:\Program Files\VTFThumbnail\
   (проводник загружает DLL оттуда - не удаляйте и не перемещайте её потом).
2. Правый клик по install.cmd > Запуск от имени администратора -> для всех,
   или просто двойной клик по install.cmd                      -> только для вас.
3. Откройте папку с .vtf (Вид > Крупные значки).

УДАЛЕНИЕ
Запустите uninstall.cmd (от администратора, если ставили для всех), затем удалите
папку. Если Windows пишет, что DLL занята, выйдите из системы и войдите снова.

АЛЬФА
Прозрачность показывается у текстур с альфой, кроме карт нормалей / SSBump.
Изменить: реестр, HKEY_CURRENT_USER\Software\VTFThumbnail, DWORD AlphaMode:
0 = авто (по умолчанию), 1 = всегда прозрачность, 2 = всегда непрозрачно.

License: MIT (see LICENSE.txt).
