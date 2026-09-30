# Окно установщика Telegator для dmgbuild; вызывает package_mac.sh.
# defines: app — путь к Telegator.app, background — фон из background.py.
import os.path

app = defines["app"]  # noqa: F821
files = [app]
symlinks = {"Программы": "/Applications"}
background = defines["background"]  # noqa: F821
# LZFSE: распаковка при копировании — секунды; bzip2 (UDBZ) копировался ~30 с.
format = "ULFO"
# Высота с запасом: Finder с вкладками съедает ~60 точек сверху.
window_rect = ((200, 120), (640, 520))
icon_size = 96
text_size = 13
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
default_view = "icon-view"
# Центры значков — те же, что у стрелки в background.py.
icon_locations = {os.path.basename(app): (170, 190), "Программы": (470, 190)}
