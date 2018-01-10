@echo off
::
::
::    Copyright (C) 2017 Fredrik Öhrström
::
::    This program is free software: you can redistribute it and/or modify
::    it under the terms of the GNU General Public License as published by
::    the Free Software Foundation, either version 3 of the License, or
::    (at your option) any later version.
::
::    This program is distributed in the hope that it will be useful,
::    but WITHOUT ANY WARRANTY; without even the implied warranty of
::    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
::    GNU General Public License for more details.
::
::    You should have received a copy of the GNU General Public License
::    along with this program.  If not, see <http://www.gnu.org/licenses/>.
::

echo Running winapi tests for Beak

set BEAK=%cd%\%1
%BEAK% help 1>NUL 2>NUL
IF %ERRORLEVEL% NEQ 0 (
   echo Please supply beak binary to be tested as first argument to test.bat
   pause
   exit
  )

set "prefix=%temp%\beak_test"
set x=%random%
set y=0
set "tmpdir=Unexpected Error"

:loop
set /a y+=1
set "tmpdir=%prefix%%x%_%y%"
if exist %tmpdir% goto loop

echo %tmpdir%
mkdir %tmpdir%



:: basic01

mkdir %tmpdir%\basic01
mkdir %tmpdir%\basic01\Root
mkdir %tmpdir%\basic01\Root\Sub
echo Hejsan > %tmpdir%\basic01\Root\Sub\Alfa

mkdir %tmpdir%\basic01\Mount

%BEAK% store %tmpdir%\basic01\Root %tmpdir%\basic01\Mount

dir /s /b %tmpdir%\basic01\Root
dir /s /b %tmpdir%\basic01\Mount

pause
