#!/usr/bin/env python3

import sys
import pathlib

sys.path.append(str(pathlib.Path(__file__).resolve().parent))

import signal, os
import PySide6.QtWidgets as QtWidgets
from Window1 import MainWindow1

def main(args):
    os.environ["QT_LOGGING_RULES"]="*.debug=false;qt.qpa.*=false"
    os.environ["QT_USE_PHYSICAL_DPI"]="1"
    qtApp = QtWidgets.QApplication(args)
    signal.signal(signal.SIGINT,signal.SIG_DFL)
    mainWin1 = MainWindow1()
    mainWin1.show()
    return qtApp.exec()


if __name__ == '__main__':
    sys.exit(main(sys.argv) or 0)
