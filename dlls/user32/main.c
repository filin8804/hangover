/*
 * Copyright 2017 André Hentschel
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* NOTE: The guest side uses mingw's headers. The host side uses Wine's headers. */

#include <windows.h>
#include <stdio.h>

#include "windows-user-services.h"
#include "dll_list.h"
#include "user32.h"

#ifndef QEMU_DLL_GUEST
#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(qemu_user32);

const struct qemu_ops *qemu_ops;

static const syscall_handler dll_functions[] =
{
    qemu_ActivateKeyboardLayout,
    qemu_AddClipboardFormatListener,
    qemu_AttachThreadInput,
    qemu_BlockInput,
    qemu_CallMsgFilterA,
    qemu_CallMsgFilterW,
    qemu_CallNextHookEx,
    qemu_ChangeClipboardChain,
    qemu_CheckDlgButton,
    qemu_CheckRadioButton,
    qemu_ClipCursor,
    qemu_CloseClipboard,
    qemu_CopyIcon,
    qemu_CopyImage,
    qemu_CountClipboardFormats,
    qemu_CreateCaret,
    qemu_CreateCursor,
    qemu_CreateDialogIndirectParamA,
    qemu_CreateDialogIndirectParamAorW,
    qemu_CreateDialogIndirectParamW,
    qemu_CreateDialogParamA,
    qemu_CreateDialogParamW,
    qemu_CreateIconIndirect,
    qemu_DefDlgProcA,
    qemu_DefDlgProcW,
    qemu_DefRawInputProc,
    qemu_DefWindowProcA,
    qemu_DefWindowProcW,
    qemu_DestroyCaret,
    qemu_DestroyCursor,
    qemu_DestroyIcon,
    qemu_DialogBoxIndirectParamA,
    qemu_DialogBoxIndirectParamAorW,
    qemu_DialogBoxIndirectParamW,
    qemu_DialogBoxParamA,
    qemu_DialogBoxParamW,
    qemu_DlgDirListA,
    qemu_DlgDirListComboBoxA,
    qemu_DlgDirListComboBoxW,
    qemu_DlgDirListW,
    qemu_DlgDirSelectComboBoxExA,
    qemu_DlgDirSelectComboBoxExW,
    qemu_DlgDirSelectExA,
    qemu_DlgDirSelectExW,
    qemu_DrawIcon,
    qemu_DrawIconEx,
    qemu_EmptyClipboard,
    qemu_EndDialog,
    qemu_EnumClipboardFormats,
    qemu_GetActiveWindow,
    qemu_GetAsyncKeyState,
    qemu_GetCapture,
    qemu_GetCaretBlinkTime,
    qemu_GetCaretPos,
    qemu_GetClassInfoA,
    qemu_GetClassInfoExA,
    qemu_GetClassInfoExW,
    qemu_GetClassInfoW,
    qemu_GetClassLongA,
    qemu_GetClassLongPtrA,
    qemu_GetClassLongPtrW,
    qemu_GetClassLongW,
    qemu_GetClassNameA,
    qemu_GetClassNameW,
    qemu_GetClassWord,
    qemu_GetClipboardData,
    qemu_GetClipboardFormatNameA,
    qemu_GetClipboardFormatNameW,
    qemu_GetClipboardOwner,
    qemu_GetClipboardSequenceNumber,
    qemu_GetClipboardViewer,
    qemu_GetClipCursor,
    qemu_GetComboBoxInfo,
    qemu_GetCursor,
    qemu_GetCursorFrameInfo,
    qemu_GetCursorInfo,
    qemu_GetCursorPos,
    qemu_GetDlgCtrlID,
    qemu_GetDlgItem,
    qemu_GetDlgItemInt,
    qemu_GetDlgItemTextA,
    qemu_GetDlgItemTextW,
    qemu_GetFocus,
    qemu_GetForegroundWindow,
    qemu_GetIconInfo,
    qemu_GetInputState,
    qemu_GetKBCodePage,
    qemu_GetKeyboardLayout,
    qemu_GetKeyboardLayoutNameA,
    qemu_GetKeyboardLayoutNameW,
    qemu_GetKeyboardState,
    qemu_GetKeyboardType,
    qemu_GetKeyNameTextA,
    qemu_GetKeyNameTextW,
    qemu_GetKeyState,
    qemu_GetLastInputInfo,
    qemu_GetListBoxInfo,
    qemu_GetNextDlgGroupItem,
    qemu_GetNextDlgTabItem,
    qemu_GetOpenClipboardWindow,
    qemu_GetPhysicalCursorPos,
    qemu_GetPriorityClipboardFormat,
    qemu_GetProgmanWindow,
    qemu_GetQueueStatus,
    qemu_GetRawInputBuffer,
    qemu_GetRawInputData,
    qemu_GetRawInputDeviceInfoA,
    qemu_GetRawInputDeviceInfoW,
    qemu_GetRawInputDeviceList,
    qemu_GetRegisteredRawInputDevices,
    qemu_GetShellWindow,
    qemu_GetTaskmanWindow,
    qemu_GetUpdatedClipboardFormats,
    qemu_HideCaret,
    qemu_IsClipboardFormatAvailable,
    qemu_IsDialogMessageW,
    qemu_IsDlgButtonChecked,
    qemu_IsWinEventHookInstalled,
    qemu_keybd_event,
    qemu_LoadBitmapA,
    qemu_LoadBitmapW,
    qemu_LoadCursorA,
    qemu_LoadCursorFromFileA,
    qemu_LoadCursorFromFileW,
    qemu_LoadCursorW,
    qemu_LoadIconA,
    qemu_LoadIconW,
    qemu_LoadImageA,
    qemu_LoadImageW,
    qemu_LoadKeyboardLayoutA,
    qemu_LoadKeyboardLayoutW,
    qemu_LookupIconIdFromDirectory,
    qemu_LookupIconIdFromDirectoryEx,
    qemu_MapDialogRect,
    qemu_MapVirtualKeyA,
    qemu_MapVirtualKeyExA,
    qemu_MapVirtualKeyExW,
    qemu_MapVirtualKeyW,
    qemu_mouse_event,
    qemu_NotifyWinEvent,
    qemu_OemKeyScan,
    qemu_OemToCharA,
    qemu_OpenClipboard,
    qemu_PaintDesktop,
    qemu_PrivateExtractIconExA,
    qemu_PrivateExtractIconExW,
    qemu_PrivateExtractIconsA,
    qemu_PrivateExtractIconsW,
    qemu_RealGetWindowClassA,
    qemu_RealGetWindowClassW,
    qemu_RegisterClassA,
    qemu_RegisterClassExA,
    qemu_RegisterClassExW,
    qemu_RegisterClassW,
    qemu_RegisterClipboardFormatA,
    qemu_RegisterClipboardFormatW,
    qemu_RegisterHotKey,
    qemu_ReleaseCapture,
    qemu_RemoveClipboardFormatListener,
    qemu_SendDlgItemMessageA,
    qemu_SendDlgItemMessageW,
    qemu_SendInput,
    qemu_SetActiveWindow,
    qemu_SetCapture,
    qemu_SetCaretBlinkTime,
    qemu_SetCaretPos,
    qemu_SetClassLongA,
    qemu_SetClassLongPtrA,
    qemu_SetClassLongPtrW,
    qemu_SetClassLongW,
    qemu_SetClassWord,
    qemu_SetClipboardData,
    qemu_SetClipboardViewer,
    qemu_SetCursor,
    qemu_SetCursorPos,
    qemu_SetDeskWallPaper,
    qemu_SetDlgItemInt,
    qemu_SetDlgItemTextA,
    qemu_SetDlgItemTextW,
    qemu_SetFocus,
    qemu_SetForegroundWindow,
    qemu_SetKeyboardState,
    qemu_SetPhysicalCursorPos,
    qemu_SetProgmanWindow,
    qemu_SetShellWindow,
    qemu_SetShellWindowEx,
    qemu_SetSystemCursor,
    qemu_SetTaskmanWindow,
    qemu_SetWindowsHookA,
    qemu_SetWindowsHookExA,
    qemu_SetWindowsHookExW,
    qemu_SetWindowsHookW,
    qemu_SetWinEventHook,
    qemu_ShowCaret,
    qemu_ShowCursor,
    qemu_ToAscii,
    qemu_ToAsciiEx,
    qemu_ToUnicode,
    qemu_ToUnicodeEx,
    qemu_TrackMouseEvent,
    qemu_UnhookWindowsHook,
    qemu_UnhookWindowsHookEx,
    qemu_UnhookWinEvent,
    qemu_UnloadKeyboardLayout,
    qemu_UnregisterClassA,
    qemu_UnregisterClassW,
    qemu_UnregisterHotKey,
    qemu_VkKeyScanA,
    qemu_VkKeyScanW,
};

const WINAPI syscall_handler *qemu_dll_register(const struct qemu_ops *ops, uint32_t *dll_num)
{
    WINE_TRACE("Loading host-side user32 wrapper.\n");

    qemu_ops = ops;
    *dll_num = QEMU_CURRENT_DLL;

    return dll_functions;
}

#endif
