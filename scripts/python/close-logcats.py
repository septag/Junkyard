import argparse
import sys
import win32file, win32pipe, pywintypes, win32api, win32gui, win32con

arg_parser = argparse.ArgumentParser(description='')
arg_parser.add_argument('--package', help='Package name', required=True)
args = arg_parser.parse_args(sys.argv[1:])

while 1:
    hwnd = win32gui.FindWindow('ConsoleWindowClass', args.package)
    if hwnd != 0:
        win32gui.SendMessage(hwnd, win32con.WM_CLOSE, 0, 0)
    else:
        break


