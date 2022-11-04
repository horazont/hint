import aioxmpp.callbacks

import PyQt5.Qt as Qt


class DisplayWidget(Qt.QWidget):
    WIDTH = 320
    HEIGHT = 240

    on_mouse_event = aioxmpp.callbacks.Signal()

    def __init__(self, parent=None):
        super().__init__(parent)

        self._surface = Qt.QImage(
            self.WIDTH, self.HEIGHT,
            Qt.QImage.Format_ARGB32_Premultiplied
        )

        self.fill_rect(
            Qt.QColor(0, 0, 0, 255),
            0, 0,
            self.WIDTH-1, self.HEIGHT-1
        )

    def draw_rect(self, colour, x0, y0, x1, y1):
        p = Qt.QPainter(self._surface)
        p.setBrush(Qt.QBrush())
        p.setPen(colour)
        p.drawRect(x0, y0, (x1-x0), (y1-y0))
        del p

        self.update()

    def set_pixel(self, colour, x, y):
        self._surface.setPixelColor(x, y, colour)

    def fill_rect(self, colour, x0, y0, x1, y1):
        p = Qt.QPainter(self._surface)
        p.setBrush(Qt.QBrush(colour))
        p.setPen(colour)
        p.drawRect(x0, y0, (x1-x0), (y1-y0))
        del p

        self.update()

    def draw_line(self, colour, x0, y0, x1, y1):
        p = Qt.QPainter(self._surface)
        p.setPen(colour)
        p.drawLine(x0, y0, x1, y1)
        del p

        self.update()

    def draw_text(self, colour, font, x0, y0, text):
        p = Qt.QPainter(self._surface)
        p.setPen(colour)
        p.setFont(font)
        p.drawText(Qt.QPoint(x0, y0), text)
        del p

        self.update()

    def draw_text_rect(self, colour, font, x0, y0, x1, y1, text, flags):
        p = Qt.QPainter(self._surface)
        p.setPen(colour)
        p.setFont(font)
        p.drawText(x0, y0, (x1-x0), (y1-y0), flags, text)
        del p

        self.update()

    def mousePressEvent(self, ev):
        x0 = round((self.width() - self.WIDTH) / 2)
        y0 = round((self.height() - self.HEIGHT) / 2)
        x = round(ev.localPos().x() - x0)
        y = round(ev.localPos().y() - y0)
        if not (0 <= x < self.WIDTH) or not (0 <= y < self.HEIGHT):
            print("out of bounds: {} {} {} {}".format(
                ev.localPos().x(),
                ev.localPos().y(),
                x, y
            ))
            return super().mousePressEvent(ev)
        self.on_mouse_event(x, y, 2**15-1)

    def mouseReleaseEvent(self, ev):
        x0 = round((self.width() - self.WIDTH) / 2)
        y0 = round((self.height() - self.HEIGHT) / 2)
        x = round(ev.localPos().x() - x0)
        y = round(ev.localPos().y() - y0)
        if not (0 <= x < self.WIDTH) or not (0 <= y < self.HEIGHT):
            x, y = 0, 0
        self.on_mouse_event(x, y, 0)
        return super().mouseReleaseEvent(ev)

    def paintEvent(self, ev):
        p = Qt.QPainter(self)
        # scaled = self._surface.scaled(
        #     self.size(),
        #     Qt.Qt.KeepAspectRatio,
        # )
        scaled = self._surface
        x0 = round((self.width() - scaled.width()) / 2)
        y0 = round((self.height() - scaled.height()) / 2)
        p.drawImage(x0, y0, scaled)
        del p
