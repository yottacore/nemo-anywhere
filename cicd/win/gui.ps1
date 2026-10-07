##	Purpose:
##		- Small GUI driver for the running app: screenshot, window rects,
##		  raise, click, key, type. Finds the app's window by enumerating the
##		  pid's visible windows and taking the largest (MainWindowHandle often
##		  answers a hidden 1x1 GTK toplevel).
##		- Run under powershell.exe 5.1, not pwsh - Add-Type System.Drawing
##		  needs a pile of assembly references on .NET Core.
##		- Syntax:
##		  powershell cicd/win/gui.ps1 shot <pid> <png> | shotwin <hwnd> <png> |
##		    desk <png> |
##		    rect <pid> |
##		    wait <pid> <secs> | raise <pid> | raisewin <hwnd> |
##		    click <x> <y> [right] | move <x> <y> |
##		    key <vk> [alt|ctrl|shift] | type <text>
##	History:
##		- 2026-09-03: desk, for menus and other windows the app does not own; move,
##		  for hover.
##		- 2026-08-30: Created (backlog: GUI testing without touching the live session).

##	Copyright (c) 2026 t00mietum
##	Licensed under The MIT License (MIT). Full text at:
##		https://mit-license.org/
##	SPDX-License-Identifier: MIT

param([string]$Cmd, [string]$A, [string]$B, [string]$C)

Add-Type -ReferencedAssemblies System.Drawing, System.Windows.Forms -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Text;
public static class G {
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
	public delegate bool EnumProc(IntPtr h, IntPtr l);
	[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
	[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
	[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
	[DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
	[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, UIntPtr e);
	[DllImport("user32.dll")] public static extern short VkKeyScanW(char c);
	[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
	[DllImport("user32.dll")] public static extern IntPtr SetProcessDpiAwarenessContext(IntPtr v);
	[DllImport("user32.dll")] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
	public static IntPtr Largest(uint pid) {
		IntPtr best = IntPtr.Zero; long bestArea = -1;
		EnumWindows((h, l) => {
			uint p; GetWindowThreadProcessId(h, out p);
			if (p != pid || !IsWindowVisible(h)) return true;
			RECT r; GetWindowRect(h, out r);
			long area = (long)(r.R - r.L) * (r.B - r.T);
			if (area > bestArea) { bestArea = area; best = h; }
			return true;
		}, IntPtr.Zero);
		return best;
	}
	public static List<string> Windows(uint pid) {
		var list = new List<string>();
		EnumWindows((h, l) => {
			uint p; GetWindowThreadProcessId(h, out p);
			if (p != pid || !IsWindowVisible(h)) return true;
			RECT r; GetWindowRect(h, out r);
			var sb = new StringBuilder(256); GetWindowTextW(h, sb, 256);
			list.Add(h + " " + r.L + "," + r.T + " " + (r.R - r.L) + "x" + (r.B - r.T) + " " + sb);
			return true;
		}, IntPtr.Zero);
		return list;
	}
	public static void Shot(IntPtr h, string path) {
		RECT r; GetWindowRect(h, out r);
		var bmp = new Bitmap(r.R - r.L, r.B - r.T);
		using (var g = Graphics.FromImage(bmp)) {
			IntPtr dc = g.GetHdc();
			PrintWindow(h, dc, 2);
			g.ReleaseHdc(dc);
		}
		bmp.Save(path, ImageFormat.Png);
	}
	/* Menus are their own windows, so PrintWindow on the app misses them. */
	public static void Desk(string path) {
		var b = System.Windows.Forms.SystemInformation.VirtualScreen;
		var bmp = new Bitmap(b.Width, b.Height);
		using (var g = Graphics.FromImage(bmp)) { g.CopyFromScreen(b.X, b.Y, 0, 0, bmp.Size); }
		bmp.Save(path, ImageFormat.Png);
	}
	public static void Raise(IntPtr h) {
		SetWindowPos(h, new IntPtr(-1), 0, 0, 0, 0, 0x0003);
		SetWindowPos(h, new IntPtr(-2), 0, 0, 0, 0, 0x0003);
	}
	/* Two steps: one SetCursorPos from a standing start can arrive without the
	   motion a tooltip waits for. */
	public static void Move(int x, int y) {
		SetCursorPos(x - 40, y - 40);
		System.Threading.Thread.Sleep(80);
		SetCursorPos(x, y);
	}
	public static void Click(int x, int y, bool right) {
		SetCursorPos(x, y);
		System.Threading.Thread.Sleep(60);
		if (right) { mouse_event(0x0008, 0, 0, 0, UIntPtr.Zero); mouse_event(0x0010, 0, 0, 0, UIntPtr.Zero); }
		else { mouse_event(0x0002, 0, 0, 0, UIntPtr.Zero); mouse_event(0x0004, 0, 0, 0, UIntPtr.Zero); }
	}
	public static void Key(byte vk, bool alt, bool ctrl, bool shift) {
		if (alt) keybd_event(0x12, 0, 0, UIntPtr.Zero);
		if (ctrl) keybd_event(0x11, 0, 0, UIntPtr.Zero);
		if (shift) keybd_event(0x10, 0, 0, UIntPtr.Zero);
		keybd_event(vk, 0, 0, UIntPtr.Zero); keybd_event(vk, 0, 2, UIntPtr.Zero);
		if (shift) keybd_event(0x10, 0, 2, UIntPtr.Zero);
		if (ctrl) keybd_event(0x11, 0, 2, UIntPtr.Zero);
		if (alt) keybd_event(0x12, 0, 2, UIntPtr.Zero);
	}
	public static void Type(string s) {
		foreach (char c in s) {
			short v = VkKeyScanW(c);
			byte vk = (byte)(v & 0xFF); bool shift = (v & 0x100) != 0;
			Key(vk, false, false, shift);
			System.Threading.Thread.Sleep(20);
		}
	}
}
'@

[G]::SetProcessDpiAwarenessContext([IntPtr]-4) | Out-Null

switch ($Cmd) {
	'shot'  { $h = [G]::Largest([uint32]$A); [G]::Shot($h, $B); "shot $h -> $B" }
	'shotwin' { [G]::Shot([IntPtr][int]$A, $B); "shot $A -> $B" }
	'desk'  { [G]::Desk($A); "desk -> $A" }
	'rect'  { [G]::Windows([uint32]$A) }
	'wait'  {
		$deadline = (Get-Date).AddSeconds([int]$B)
		while ((Get-Date) -lt $deadline) {
			$h = [G]::Largest([uint32]$A)
			if ($h -ne [IntPtr]::Zero) { "win $h"; exit 0 }
			Start-Sleep -Milliseconds 500
		}
		"timeout"; exit 1
	}
	'raise' { $h = [G]::Largest([uint32]$A); [G]::Raise($h); "raised $h" }
	'raisewin' { [G]::Raise([IntPtr][int]$A); "raised $A" }
	'click' { [G]::Click([int]$A, [int]$B, ($C -eq 'right')); "clicked $A,$B" }
	'move'  { [G]::Move([int]$A, [int]$B); "moved $A,$B" }
	'key'   { [G]::Key([byte][int]$A, ($B -match 'alt'), ($B -match 'ctrl'), ($B -match 'shift')); "key $A $B" }
	'type'  { [G]::Type($A); "typed" }
	default { "usage: shot|shotwin|rect|wait|raise|raisewin|click|key|type" }
}
