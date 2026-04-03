from __future__ import annotations

import logging
from typing import Any, Callable

from PySide6.QtCore import QObject, QRunnable, Signal


class WorkerSignals(QObject):
    success = Signal(object)
    error = Signal(str)
    finished = Signal()


class Worker(QRunnable):
    def __init__(self, fn: Callable[[], Any]) -> None:
        super().__init__()
        self.fn = fn
        self.signals = WorkerSignals()
        self._log = logging.getLogger(__name__)

    def run(self) -> None:
        try:
            result = self.fn()
            self.signals.success.emit(result)
        except Exception as exc:  # pylint: disable=broad-except
            self._log.exception("Worker exception")
            self.signals.error.emit(str(exc))
        finally:
            self.signals.finished.emit()
