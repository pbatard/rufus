## Rufus: Надежная утилита форматирования USB
==========================================

[![Статус сборки VS2022](https://img.shields.io/github/actions/workflow/status/pbatard/rufus/vs2022.yml?branch=master&style=flat-square&label=VS2022%20Build)](https://github.com/pbatard/rufus/actions/workflows/vs2022.yml)
[![Статус сборки MinGW](https://img.shields.io/github/actions/workflow/status/pbatard/rufus/mingw.yml?branch=master&style=flat-square&label=MinGW%20Build)](https://github.com/pbatard/rufus/actions/workflows/mingw.yml)
[![Статус анализа Coverity](https://img.shields.io/coverity/scan/2172.svg?style=flat-square&label=Coverity%20Analysis)](https://scan.coverity.com/projects/pbatard-rufus)  
[![Последняя версия](https://img.shields.io/github/release-pre/pbatard/rufus.svg?style=flat-square&label=Latest%20Release)](https://github.com/pbatard/rufus/releases)
[![Лицензия](https://img.shields.io/badge/license-GPLv3-blue.svg?style=flat-square&label=License)](https://www.gnu.org/licenses/gpl-3.0.en.html)
[![Статистика загрузок](https://img.shields.io/github/downloads/pbatard/rufus/total.svg?label=Downloads&style=flat-square)](https://github.com/pbatard/rufus/releases)
[![Участники](https://img.shields.io/github/contributors/pbatard/rufus.svg?style=flat-square&label=Contributors)](https://github.com/pbatard/rufus/graphs/contributors)

![Логотип Rufus](https://raw.githubusercontent.com/pbatard/rufus/master/res/icons/rufus-128.png)

Rufus — это утилита для форматирования и создания загрузочных USB-накопителей.

### Возможности

* Форматирование USB-накопителей, карт памяти и виртуальных дисков в FAT/FAT32/NTFS/UDF/exFAT/ReFS/ext2/ext3
* Создание загрузочных USB-накопителей DOS с использованием [FreeDOS](https://www.freedos.org) или MS-DOS
* Создание загрузочных накопителей для BIOS или UEFI, включая [UEFI-загрузочные NTFS](https://github.com/pbatard/uefi-ntfs)
* Создание загрузочных накопителей из загрузочных ISO-файлов (Windows, Linux и др.)
* Создание загрузочных накопителей из образов дисков, включая сжатые
* Создание установочных накопителей Windows 11 для компьютеров без TPM или Secure Boot
* Создание [Windows To Go](https://en.wikipedia.org/wiki/Windows_To_Go) накопителей
* Создание образов VHD/DD, VHDX и FFU существующего диска
* Создание постоянных разделов Linux
* Вычисление контрольных сумм MD5, SHA-1, SHA-256 и SHA-512 выбранного образа
* Проверка работоспособности UEFI-загрузочных носителей
* Улучшение процесса установки Windows путем автоматической настройки параметров OOBE (локальная учетная запись, параметры конфиденциальности и т.д.)
* Проверка на наличие поврежденных блоков, включая обнаружение "поддельных" флеш-накопителей
* Загрузка официальных ISO-файлов Microsoft Windows 8, Windows 10 или Windows 11
* Загрузка [UEFI Shell](https://github.com/pbatard/UEFI-Shell) ISO-файлов
* Современный и понятный интерфейс с [родной поддержкой 38 языков](https://github.com/pbatard/rufus/wiki/FAQ#What_languages_are_natively_supported_by_Rufus)
* Компактный размер. Не требует установки.
* Портативность. Совместимость с Secure Boot.
* 100% [Свободное программное обеспечение](https://www.gnu.org/philosophy/free-sw) ([GPL v3](https://www.gnu.org/licenses/gpl-3.0))

### Компиляция

Используйте Visual Studio 2022 или MinGW, затем запустите `.sln` или `configure`/`make` соответственно.
