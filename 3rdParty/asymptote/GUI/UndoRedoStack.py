#!/usr/bin/env python3
###########################################################################
#
# UndoRedoStack implements the usual undo/redo capabilities of a GUI
#
# Author: Orest Shardt
# Created: July 23, 2007
#
###########################################################################


class action:
    def __init__(self, actions):
        act, inv = actions
        self.act = act
        self.inv = inv

    def undo(self):
        # print ("Undo:",self)
        self.inv()

    def redo(self):
        # print ("Redo:",self)
        self.act()

    def __str__(self):
        return "A generic action"


class beginActionGroup:
    pass


class endActionGroup:
    pass


class actionStack:
    def __init__(self):
        self.clear()
        self.redoStack = []

    def add(self, action_to_add):
        self.undoStack.append(action_to_add)
        # print ("Added",action)
        self.redoStack.clear()

    def undo(self):
        if len(self.undoStack) > 0:
            op = self.undoStack.pop()
            if op is beginActionGroup:
                level = 1
                self.redoStack.append(endActionGroup)
                while level > 0:
                    op = self.undoStack.pop()
                    if op is endActionGroup:
                        level -= 1
                        self.redoStack.append(beginActionGroup)
                    elif op is beginActionGroup:
                        level += 1
                        self.redoStack.append(endActionGroup)
                    else:
                        op.undo()
                        self.redoStack.append(op)
            elif op is endActionGroup:
                raise Exception("endActionGroup without previous beginActionGroup")
            else:
                self.redoStack.append(op)
                op.undo()
                # print ("undid",op)
        else:
            pass  # print ("nothing to undo")

    def redo(self):
        if len(self.redoStack) > 0:
            op = self.redoStack.pop()
            if op is beginActionGroup:
                level = 1
                self.undoStack.append(endActionGroup)
                while level > 0:
                    op = self.redoStack.pop()
                    if op is endActionGroup:
                        level -= 1
                        self.undoStack.append(beginActionGroup)
                    elif op is beginActionGroup:
                        level += 1
                        self.undoStack.append(endActionGroup)
                    else:
                        op.redo()
                        self.undoStack.append(op)
            elif op is endActionGroup:
                raise Exception("endActionGroup without previous beginActionGroup")
            else:
                self.undoStack.append(op)
                op.redo()
                # print ("redid",op)
        else:
            pass  # print ("nothing to redo")

    def setCommitLevel(self):
        self.commitLevel = len(self.undoStack)

    def changesMade(self):
        if len(self.undoStack) != self.commitLevel:
            return True
        else:
            return False

    def clear(self):
        self.redoStack = []
        self.undoStack = []
        self.commitLevel = 0
