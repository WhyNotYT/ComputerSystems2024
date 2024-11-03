import time
from pywinauto.application import Application
from pywinauto import mouse

# CCS Path
ccs_exe_path = r"D:\Softwares\TiShit2\ccs\eclipse\ccstudio.exe"

# Project Path
project_path = (
    r"C:\Users\yumis\Downloads\empty_CC2650STK_TI_2023\empty_CC2650STK_TI_2023"
)


def open_ccs():
    # Launch Code Composer Studio
    app = Application(backend="win32").start(ccs_exe_path)

    # Give it time to open
    time.sleep(15)

    # Connect to the opened CCS window
    app = Application(backend="win32").connect(path=ccs_exe_path)
    main_window = app.window(title_re=".*Code Composer Studio.*")

    return main_window


def open_project(main_window):
    # Click on the "Project" menu
    main_window.menu_select("Project->Import CCS Projects...")

    # Wait for the Import Dialog
    import_dialog = main_window.child_window(title_re="Import.*", control_type="Window")
    import_dialog.wait("ready", timeout=30)

    # Set the path of the project folder
    path_edit = import_dialog.child_window(auto_id="location", control_type="Edit")
    path_edit.set_edit_text(project_path)

    # Click the "Finish" button to import the project
    finish_button = import_dialog.child_window(title="Finish", control_type="Button")
    finish_button.click()

    # Wait for the import to complete
    time.sleep(10)


def start_debug(main_window):
    # Click on the "Run" menu and select "Debug"
    main_window.menu_select("Run->Debug")

    # Wait for the debug session to start
    time.sleep(10)


if __name__ == "__main__":
    main_window = open_ccs()  # Open CCS
    open_project(main_window)  # Import the project
    start_debug(main_window)  # Start debugging
