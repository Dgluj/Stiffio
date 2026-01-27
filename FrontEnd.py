"""FRONTEND.PY
Interfaz de usuario"""

# =================================================================================================
# Librerías
# =================================================================================================
import sys
import os
import pyqtgraph as pg
import numpy as np
import time
import csv
from reportlab.lib.pagesizes import A4
from reportlab.pdfgen import canvas


from datetime import datetime

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QHBoxLayout, QVBoxLayout,
    QLabel, QLineEdit, QPushButton, QComboBox, QStatusBar, QMessageBox, QDialog, QDateEdit, QDialogButtonBox, QFileDialog, QTextEdit)
from PyQt6.QtCore import QTimer, Qt, QRegularExpression, QDate, QSize
from PyQt6.QtGui import QPixmap, QRegularExpressionValidator, QFont, QIcon, QTextDocument

from pyqtgraph import FillBetweenItem

from PyQt6.QtWidgets import QTableWidget, QTableWidgetItem

# Importar el backend y la comunicación
from BackEnd import processor
import ComunicacionMax

from PyQt6.QtPrintSupport import QPrinter


# =================================================================================================
# Ventana de Inicio
# =================================================================================================
class WelcomeScreen(QWidget):

    # Layout --------------------------------------------------------------------------------------
    def __init__(self):
        super().__init__() # Inicializar la ventana

        self.setWindowTitle("Stiffio") # Título
        self.setGeometry(100, 100, 800, 600) # Tamaño
        self.setStyleSheet("background-color: black; color: white;") # Color de Fondo
        layout = QVBoxLayout()  # Layout
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setLayout(layout)


        # Logo -----------------------------------------------------------
        logo_path = "Logo invertido.png" # Si está en la misma carpeta alcanza solo con el nombre del archivo
        if os.path.exists(logo_path):
            logo_label = QLabel()
            pixmap = QPixmap(logo_path)
            pixmap = pixmap.scaled(700, 250, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation) # Tamaño
            logo_label.setPixmap(pixmap)
            logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter) # Posición
            layout.addWidget(logo_label)

        # Texto -----------------------------------------------------------
        text_label = QLabel("Sistema de Medición de Rigidez Arterial")
        text_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        text_label.setStyleSheet("font-size: 16pt; font-weight: bold;")
        layout.addWidget(text_label)
        layout.addSpacing(50)

        # Botón -----------------------------------------------------------
        start_button = QPushButton("Comenzar")
        start_button.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50; /* Verde */
                color: white;
                font-size: 14pt;
                padding: 15px 30px;
                border-radius: 5px;
                font-weight: bold;
            }

            QPushButton:hover {
                background-color: #388E3C;
            }
        """)

        start_button.clicked.connect(self.open_patient_data_window) # Clic en comenzar
        layout.addWidget(start_button)

# boton de historial
        history_button = QPushButton("Historial de Mediciones")
        history_button.setStyleSheet("""
            QPushButton {
                background-color: #2196F3;
                color: white;
                font-size: 14pt;
                padding: 15px 30px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #1976D2;
            }
        """)
        history_button.clicked.connect(self.open_history)
        layout.addWidget(history_button)


    # Funcionalidad -------------------------------------------------------------------------------
    def open_patient_data_window(self):
        from __main__ import PatientDataScreen
        self.patient_data_window = PatientDataScreen()

        # Mantener tamaño y posición de la ventana actual
        self.patient_data_window.resize(self.size())
        self.patient_data_window.move(self.pos())
        if self.isMaximized():
            self.patient_data_window.showMaximized()
        else:
            self.patient_data_window.show()

        # Cerrar la ventana actual después de mostrar la nueva
        QTimer.singleShot(50, self.close)

    def open_history(self):
        from __main__ import HistoryScreen
        self.history_window = HistoryScreen()

        self.history_window.resize(self.size())
        self.history_window.move(self.pos())
        if self.isMaximized():
            self.history_window.showMaximized()
        else:
            self.history_window.show()

        QTimer.singleShot(50, self.close)


# =================================================================================================
# Ventana de Datos del Paciente
# =================================================================================================
class PatientDataScreen(QWidget):

    # Layout --------------------------------------------------------------------------------------
    def __init__(self):
        super().__init__() # Inicializar la ventana

        self.setWindowTitle("Stiffio") # Título
        self.setGeometry(100, 100, 800, 600) # Tamaño
        self.setStyleSheet("background-color: black; color: white;") # Color de Fondo
        self.layout = QVBoxLayout() # Layout
        self.layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.setLayout(self.layout)


        # Logo -----------------------------------------------------------
        logo_path = "Logo invertido.png"
        if os.path.exists(logo_path):
            logo_label = QLabel()
            pixmap = QPixmap(logo_path)
            pixmap = pixmap.scaled(100,50, Qt.AspectRatioMode.KeepAspectRatio,
                                   Qt.TransformationMode.SmoothTransformation)  # Tamaño
            logo_label.setPixmap(pixmap)
            logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.layout.addWidget(logo_label)
            self.layout.addSpacing(20)


        # Título ----------------------------------------------------------
        title_label = QLabel("Datos del paciente")
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title_label.setStyleSheet("color: white; font-size: 20pt; font-weight: bold;")
        self.layout.addWidget(title_label)
        self.layout.addSpacing(40)


        # Inputs ----------------------------------------------------------

        # Nombre
        self.name_label = QLabel("Nombre")
        self.name_label.setStyleSheet("color: white; font-size: 14pt;")
        self.name_input = QLineEdit()
        self.name_input.setStyleSheet("background-color: white; color: black; font-size: 14pt; padding: 5px;")
        self.name_input.setFixedWidth(750)  # Limitar ancho
        self.layout.addWidget(self.name_label, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addWidget(self.name_input, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addSpacing(20)

        # Edad
        self.age_label = QLabel("Edad")
        self.age_label.setStyleSheet("color: white; font-size: 14pt;")
        self.age_input = QLineEdit()
        self.age_input.setStyleSheet("background-color: white; color: black; font-size: 14pt; padding: 5px;")
        self.age_input.setMaxLength(3)
        self.age_input.setValidator(QRegularExpressionValidator(QRegularExpression(r"^[0-9]{1,3}$")))
        self.age_input.setFixedWidth(750)  # Limitar ancho
        self.layout.addWidget(self.age_label, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addWidget(self.age_input, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addSpacing(20)

        # Altura
        self.height_label = QLabel("Altura (cm)")
        self.height_label.setStyleSheet("color: white; font-size: 14pt;")
        self.height_input = QLineEdit()
        self.height_input.setStyleSheet("background-color: white; color: black; font-size: 14pt; padding: 5px;")
        self.height_input.setMaxLength(3)
        self.height_input.setValidator(QRegularExpressionValidator(QRegularExpression(r"^[0-9]{2,3}$")))
        self.height_input.setFixedWidth(750)  # Limitar ancho
        self.layout.addWidget(self.height_label, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addWidget(self.height_input, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addSpacing(20)

        # Sexo
        self.sex_label = QLabel("Sexo")
        self.sex_label.setStyleSheet("color: white; font-size: 14pt;")
        self.layout.addWidget(self.sex_label, alignment=Qt.AlignmentFlag.AlignCenter)

        self.sex_combo = QComboBox()
        self.sex_combo.addItem("")  # Item vacío inicial
        self.sex_combo.addItem("Femenino")
        self.sex_combo.addItem("Masculino")
        self.sex_combo.setStyleSheet("""
            QComboBox {
                background-color: white;
                color: black;
                font-size: 14pt;
                padding: 5px;
                selection-background-color: white;
                selection-color: black;

            }


            QComboBox QAbstractItemView {
                background-color: white;
                color: black;
                selection-background-color: #E0E0E0;
                selection-color: black;
            }

        """)

        self.sex_combo.setFixedWidth(750)  # Limitar ancho
        self.layout.addWidget(self.sex_combo, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addSpacing(20)

       # Observaciones (opcional)
        self.observations_label = QLabel("Observaciones del médico")
        self.observations_label.setStyleSheet("color: white; font-size: 12pt;")
        self.layout.addWidget(self.observations_label, alignment=Qt.AlignmentFlag.AlignCenter)

        self.observations_input = QTextEdit()
        self.observations_input.setStyleSheet("background-color: white; color: black; font-size: 10pt; padding: 5px;")
        self.observations_input.setFixedWidth(750)
        self.observations_input.setFixedHeight(50)
        self.observations_input.setPlaceholderText("Ingrese síntomas, antecedentes u otras observaciones relevantes...")
        # AGREGAR ESTA LÍNEA para evitar que el texto expanda el widget:
        self.observations_input.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOn)
        self.layout.addWidget(self.observations_input, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addSpacing(10)


        # Botones --------------------------------------------------------
        self.button_layout = QHBoxLayout()
        self.back_button = QPushButton("Volver")
        self.continue_button = QPushButton("Continuar")

        # Limitar ancho de los botones
        self.back_button.setMaximumWidth(250)
        self.continue_button.setMaximumWidth(250)


        self.continue_button.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;  /* Verde */
                color: white;
                font-size: 14pt;
                padding: 15px 30px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #388E3C;
            }
        """)

        self.back_button.setStyleSheet("""
            QPushButton {
                background-color: #F44336;  /* Rojo */
                color: white;
                font-size: 14pt;
                padding: 15px 30px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #D32F2F;
            }
        """)

        self.button_layout.addWidget(self.back_button)
        self.button_layout.addWidget(self.continue_button)
        self.layout.addLayout(self.button_layout)

        self.back_button.clicked.connect(self.go_back) # Clic en volver atrás
        self.continue_button.clicked.connect(self.go_next) # Clic en continuar

 # Funcionalidad -------------------------------------------------------------------------------

    # Volver a la ventana de inicio
    def go_back(self):
        from __main__ import WelcomeScreen
        self.welcome_screen = WelcomeScreen()

        # Mantener tamaño y posición de la ventana actual
        self.welcome_screen.resize(self.size())
        self.welcome_screen.move(self.pos())
        if self.isMaximized():
            self.welcome_screen.showMaximized()
        else:
            self.welcome_screen.show()

        # Cerrar la ventana actual después de mostrar la nueva
        QTimer.singleShot(50, self.close)


    # Continuar a la ventana principal
    def go_next(self):
        # Verificar que todos los campos estén completos
        name_ok = bool(self.name_input.text().strip())
        age_ok = bool(self.age_input.text().strip())
        height_ok = bool(self.height_input.text().strip())
        sex_ok = self.sex_combo.currentText() != ""

        if not (name_ok and age_ok and height_ok and sex_ok):
            msg = QMessageBox(self)
            msg.setIcon(QMessageBox.Icon.Warning)
            msg.setWindowTitle("Campos incompletos")
            msg.setText("Por favor, completa todos los datos antes de continuar.")
            font = QFont()
            font.setPointSize(14)  # Tamaño
            msg.setFont(font)

            ok_button = msg.addButton(QMessageBox.StandardButton.Ok) # Botón OK
            ok_button.setStyleSheet("""
                QPushButton {
                    background-color: #FFD700; /* Amarillo */
                    color: black;
                    font-size: 12pt;
                    padding: 10px 20px;
                    border-radius: 5px;
                }
                QPushButton:hover {
                    background-color: #FFC300;
                }
            """)

            msg.exec()  # Mostrar la alerta
            return  # Salir de la función, no abrir la siguiente ventana


           # ---------- Validación de edad ----------
        try:
            age_val = int(self.age_input.text())
            if not (10 <= age_val <= 80):
                raise ValueError
        except ValueError:
            msg = QMessageBox(self)
            msg.setIcon(QMessageBox.Icon.Warning)
            msg.setWindowTitle("Edad inválida")
            msg.setText("La edad ingresada es inválida.\nDebe estar entre 10 y 80 años.")
            font = QFont()
            font.setPointSize(14)
            msg.setFont(font)

            ok_button = msg.addButton(QMessageBox.StandardButton.Ok)
            ok_button.setStyleSheet("""
                QPushButton {
                    background-color: #FFD700;
                    color: black;
                    font-size: 12pt;
                    padding: 10px 20px;
                    border-radius: 5px;
                }
                QPushButton:hover {
                    background-color: #FFC300;
                }
            """)
            msg.exec()
            return

        # Verificación de altura
        try:
            height_val = int(self.height_input.text())
            if not (120 <= height_val <= 230):
                raise ValueError
        except ValueError:
            msg = QMessageBox(self)
            msg.setIcon(QMessageBox.Icon.Warning)
            msg.setWindowTitle("Altura inválida")
            msg.setText("La altura ingresada es inválida.\nDebe estar entre 120 y 230 cm.")
            font = QFont()
            font.setPointSize(14)
            msg.setFont(font)

            ok_button = msg.addButton(QMessageBox.StandardButton.Ok)
            ok_button.setStyleSheet("""
                QPushButton {
                    background-color: #FFD700;
                    color: black;
                    font-size: 12pt;
                    padding: 10px 20px;
                    border-radius: 5px;
                }
                QPushButton:hover {
                    background-color: #FFC300;
                }
            """)
            msg.exec()
            return

                # Si todas las validaciones pasan, guardar datos y continuar a la ventana principal
        patient_data = {
            "nombre": self.name_input.text(),
            "edad": self.age_input.text(),
            "altura": self.height_input.text(),
            "sexo": self.sex_combo.currentText(),
            "observaciones": self.observations_input.toPlainText().strip()
        }

        try:
            from __main__ import MainScreen
            self.main_window = MainScreen(patient_data)

            # Mantener tamaño y posición de la ventana actual
            self.main_window.resize(self.size())
            self.main_window.move(self.pos())
            if self.isMaximized():
                self.main_window.showMaximized()
            else:
                self.main_window.show()

            # Cerrar la ventana actual después de mostrar la nueva
            QTimer.singleShot(50, self.close)
            
        except Exception as e:
            msg = QMessageBox(self)
            msg.setIcon(QMessageBox.Icon.Critical)
            msg.setWindowTitle("Error")
            msg.setText(f"Error al continuar:\n{str(e)}")
            msg.exec()


class HistoryScreen(QWidget):
    def save_pdf(self, report_text):
        file_path, _ = QFileDialog.getSaveFileName(
            self,
            "Guardar reporte como PDF",
            "",
            "PDF (*.pdf)"
        )

        if not file_path:
            return

        if not file_path.lower().endswith(".pdf"):
            file_path += ".pdf"

        c = canvas.Canvas(file_path, pagesize=A4)
        width, height = A4

        # ================= LOGO =================
        logo_path = "Logo.jpg"
        if os.path.exists(logo_path):
            c.drawImage(
                logo_path,
                x=50,
                y=height - 100,
                width=140,
                height=60,
                preserveAspectRatio=True,
                mask='auto'
            )

        # ================= TÍTULO =================
        c.setFont("Helvetica-Bold", 16)
        c.drawCentredString(width / 2, height - 130, "REPORTE DE MEDICIÓN - STIFFIO")

        # ================= TEXTO =================
        text = c.beginText(50, height - 170)
        text.setFont("Helvetica", 10)

        for line in report_text.strip().split("\n"):
            text.textLine(line)

        c.drawText(text)

        c.showPage()
        c.save()

        QMessageBox.information(
            self,
            "PDF guardado",
            f"El reporte fue guardado correctamente en:\n{file_path}"
        )


    def __init__(self):
        super().__init__()
        self.setWindowTitle("Historial de Mediciones")
        self.setStyleSheet("background-color: black; color: white;")

        self.layout = QVBoxLayout()
        self.setLayout(self.layout)

        logo_path = "Logo invertido.png"
        if os.path.exists(logo_path):
            logo_label = QLabel()
            pixmap = QPixmap(logo_path)
            pixmap = pixmap.scaled(200, 100, Qt.AspectRatioMode.KeepAspectRatio,
                                   Qt.TransformationMode.SmoothTransformation)
            logo_label.setPixmap(pixmap)
            logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.layout.addWidget(logo_label)

        title = QLabel("Historial de Mediciones")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title.setStyleSheet("font-size: 30pt; font-weight: bold; margin: 20px;")
        self.layout.addWidget(title)

        search_layout = QHBoxLayout()

        self.search_input = QLineEdit()
        self.search_input.setPlaceholderText("Buscar por Nombre / ID...")
        self.search_input.setStyleSheet("""
            QLineEdit {
                background-color: white;
                color: black;
                font-size: 12pt;
                padding: 10px;
                border-radius: 5px;
                max-width: 500px;
            }
        """)
        self.search_input.textChanged.connect(self.filter_table)
        search_layout.addWidget(self.search_input)

        search_layout.addSpacing(20)

        filter_button = QPushButton("📅 Filtro por Fecha")
        filter_button.setStyleSheet("""
            QPushButton {
                background-color: #424242;
                color: white;
                font-size: 12pt;
                padding: 10px 20px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #616161;
            }
        """)
        filter_button.clicked.connect(self.open_date_filter)

        search_layout.addWidget(filter_button)

        self.layout.addLayout(search_layout)
        self.layout.addSpacing(20)

        self.load_data()

        back_button = QPushButton("Volver")
        back_button.setStyleSheet("""
            QPushButton {
                background-color: #F44336;
                color: white;
                font-size: 14pt;
                padding: 15px 30px;
                border-radius: 5px;
                font-weight: bold;
                max-width: 200px;
            }
            QPushButton:hover {
                background-color: #D32F2F;
            }
        """)
        back_button.clicked.connect(self.go_back)
        self.layout.addWidget(back_button, alignment=Qt.AlignmentFlag.AlignCenter)


    def load_data(self):
        filename = "mediciones_pwv.csv"

        if not os.path.exists(filename):
            self.show_empty_message()
            return

        with open(filename, newline='', encoding='utf-8-sig') as file:
            rows = list(csv.reader(file, delimiter=';'))

        if len(rows) <= 1:
            self.show_empty_message()
            return

        header = rows[0]
        data = rows[1:]
        self.all_data = data  # Store all data for filtering
        self.show_table(header, data)

    def show_empty_message(self):
        label = QLabel("No existen registros.\nInicie una medición.")
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        label.setStyleSheet("font-size: 18pt; color: gray;")
        self.layout.addWidget(label)

    def show_table(self, header, data):
        self.table = QTableWidget()
        self.table.setRowCount(len(data))
        self.table.setColumnCount(len(header) + 2)  # +1 for ID column, +1 for delete button

        headers_with_id = ["ID"] + header + ["Acciones"]
        self.table.setHorizontalHeaderLabels(headers_with_id)

        self.table.setStyleSheet("""
            QTableWidget {
                background-color: #1a1a1a;
                color: white;
                gridline-color: #2a2a2a;
                font-size: 11pt;
                border: none;
            }
            QTableWidget::item {
                padding: 12px;
                border-bottom: 1px solid #2a2a2a;
            }
            QTableWidget::item:alternate {
                background-color: #2a2a2a;
            }
            QHeaderView::section {
                background-color: #2a2a2a;
                color: white;
                font-weight: bold;
                font-size: 11pt;
                padding: 15px;
                border: none;
                border-bottom: 2px solid #424242;
            }
            QTableWidget::item:selected {
                background-color: #424242;
            }
        """)

        self.table.setAlternatingRowColors(True)
        self.table.verticalHeader().setVisible(False)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)


        for r, row in enumerate(data):
            # ID column
            id_item = QTableWidgetItem(f"#{str(r+1).zfill(3)}")
            id_item.setForeground(Qt.GlobalColor.gray)
            self.table.setItem(r, 0, id_item)

            # Data columns
            for c, value in enumerate(row):
                item = QTableWidgetItem(value)

                if c == 6:  # PWV column
                    try:
                        pwv_value = float(value)
                        if pwv_value < 7.0:
                            item.setForeground(Qt.GlobalColor.green)
                        else:
                            item.setForeground(Qt.GlobalColor.red)
                    except ValueError:
                        pass

                self.table.setItem(r, c + 1, item)

            actions_widget = QWidget()
            actions_layout = QHBoxLayout(actions_widget)
            actions_layout.setContentsMargins(5, 2, 5, 2)
            actions_layout.setSpacing(8)

            print_button = QPushButton()
            print_button.setIcon(QIcon("print-icon.png"))
            print_button.setIconSize(QSize(24, 24))
            print_button.setFixedSize(50, 50)
            print_button.setStyleSheet("""
                QPushButton {
                    background-color: transparent;
                    border: none;
                }
                QPushButton:hover {
                    background-color: rgba(255, 255, 255, 0.1);
                }
            """)
            print_button.clicked.connect(lambda checked, row=r: self.print_record(row))
            actions_layout.addWidget(print_button)

            delete_button = QPushButton()
            delete_button.setIcon(QIcon("delete-icon.png"))
            delete_button.setIconSize(QSize(24, 24))
            delete_button.setFixedSize(50, 50)
            delete_button.setStyleSheet("""
                QPushButton {
                    background-color: transparent;
                    border: none;
                }
                QPushButton:hover {
                    background-color: rgba(255, 255, 255, 0.1);
                }
            """)
            delete_button.clicked.connect(lambda checked, row=r: self.delete_record(row))
            actions_layout.addWidget(delete_button)

            self.table.setCellWidget(r, len(header) + 1, actions_widget)
            self.table.setRowHeight(r, 75)

        self.table.resizeColumnsToContents()
        self.table.horizontalHeader().setStretchLastSection(True)

        for i in range(self.table.columnCount()):
            if self.table.columnWidth(i) < 100:
                self.table.setColumnWidth(i, 100)
        
        actions_col = self.table.columnCount() - 1
        self.table.setColumnWidth(actions_col, 200)

        self.layout.addWidget(self.table)

    def delete_record(self, row):
        # Obtener el nombre del paciente para mostrarlo en el mensaje
        # Asumiendo que la columna con el nombre es la segunda (índice 1)
        name_item = self.table.item(row, 1)  # Columna 1 es "Fecha y Hora"
        patient_info = name_item.text() if name_item else f"Registro #{row+1}"

        # Mensaje de confirmación
        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Icon.Warning)
        msg.setWindowTitle("Confirmar Eliminación")
        msg.setText(f"¿Está seguro que desea eliminar este registro?\n\n{patient_info}")
        msg.setInformativeText("Esta acción no se puede deshacer.")

        font = QFont()
        font.setPointSize(12)
        msg.setFont(font)

        # Botones personalizados
        yes_button = msg.addButton("Sí, Eliminar", QMessageBox.ButtonRole.YesRole)
        no_button = msg.addButton("Cancelar", QMessageBox.ButtonRole.NoRole)

        yes_button.setStyleSheet("""
            QPushButton {
                background-color: #F44336;
                color: white;
                font-size: 11pt;
                padding: 8px 16px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #D32F2F;
            }
        """)

        no_button.setStyleSheet("""
            QPushButton {
                background-color: #424242;
                color: white;
                font-size: 11pt;
                padding: 8px 16px;
                border-radius: 5px;
            }
            QPushButton:hover {
                background-color: #616161;
            }
        """)

        msg.exec()

        # Si el usuario confirmed la eliminación
        if msg.clickedButton() == yes_button:
            self.perform_deletion(row)

    def perform_deletion(self, row):
        filename = "mediciones_pwv.csv"

        try:
            # Leer el archivo CSV completo
            with open(filename, 'r', newline='', encoding='utf-8-sig') as file:
                rows = list(csv.reader(file, delimiter=';'))

            # Eliminar la fila (row+1 porque row 0 es el header)
            if len(rows) > row + 1:
                del rows[row + 1]

            # Reescribir el archivo CSV sin la fila eliminada
            with open(filename, 'w', newline='', encoding='utf-8-sig') as file:
                writer = csv.writer(file, delimiter=';')
                writer.writerows(rows)

            # Recargar la tabla
            self.refresh_table()

            # Mensaje de éxito
            success_msg = QMessageBox(self)
            success_msg.setIcon(QMessageBox.Icon.Information)
            success_msg.setWindowTitle("Registro Eliminado")
            success_msg.setText("El registro ha sido eliminado exitosamente.")

            font = QFont()
            font.setPointSize(12)
            success_msg.setFont(font)

            ok_button = success_msg.addButton(QMessageBox.StandardButton.Ok)
            ok_button.setStyleSheet("""
                QPushButton {
                    background-color: #4CAF50;
                    color: white;
                    font-size: 11pt;
                    padding: 8px 16px;
                    border-radius: 5px;
                }
                QPushButton:hover {
                    background-color: #388E3C;
                }
            """)

            success_msg.exec()

        except Exception as e:
            # Mensaje de error
            error_msg = QMessageBox(self)
            error_msg.setIcon(QMessageBox.Icon.Critical)
            error_msg.setWindowTitle("Error")
            error_msg.setText(f"Error al eliminar el registro:\n{str(e)}")

            font = QFont()
            font.setPointSize(12)
            error_msg.setFont(font)

            error_msg.exec()

    def refresh_table(self):
        # Eliminar la tabla existente
        if hasattr(self, 'table'):
            self.table.setParent(None)
            self.table.deleteLater()

        # Recargar los datos
        self.load_data()

    def filter_table(self, text):
        if not hasattr(self, 'table'):
            return

        text = text.lower()
        for row in range(self.table.rowCount()):
            should_show = False
            if text == "":
                should_show = True
            else:
                # Search in ID, Name columns
                for col in range(self.table.columnCount()):
                    item = self.table.item(row, col)
                    if item and text in item.text().lower():
                        should_show = True
                        break

            self.table.setRowHidden(row, not should_show)

    def go_back(self):
        from __main__ import WelcomeScreen
        self.welcome = WelcomeScreen()
        self.welcome.resize(self.size())
        self.welcome.move(self.pos())
        self.welcome.show()
        QTimer.singleShot(50, self.close)


    def print_record(self, row):
        # Get all the data from the row
        row_data = []
        for col in range(1, self.table.columnCount() - 1):  # Skip ID and Actions columns
            item = self.table.item(row, col)
            if item:
                row_data.append(item.text())

        # Create a formatted print dialog
        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Icon.Information)
        msg.setWindowTitle("Guardar Registro")

        # Format the data nicely
        fecha_hora = row_data[0] if len(row_data) > 0 else "N/A"
        nombre = row_data[1] if len(row_data) > 1 else "N/A"
        edad = row_data[2] if len(row_data) > 2 else "N/A"
        altura = row_data[3] if len(row_data) > 3 else "N/A"
        sexo = row_data[4] if len(row_data) > 4 else "N/A"
        hr = row_data[5] if len(row_data) > 5 else "N/A"
        pwv = row_data[6] if len(row_data) > 6 else "N/A"
        observaciones = row_data[7] if len(row_data) > 7 else ""

        # Determine PWV status color
        pwv_status = ""
        try:
            pwv_value = float(pwv)
            if pwv_value < 7.0:
                pwv_status = " (Saludable)"
            else:
                pwv_status = " (Elevado)"
        except ValueError:
            pass
    
        # Construir el texto del reporte
        report_text = f"""

REPORTE DE MEDICIÓN - STIFFIO

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Fecha y Hora: {fecha_hora}

DATOS DEL PACIENTE:
  • Nombre: {nombre}
  • Edad: {edad} años
  • Altura: {altura} cm
  • Sexo: {sexo}

RESULTADOS DE LA MEDICIÓN:
  • Frecuencia Cardíaca: {hr} bpm
  • PWV (Velocidad de Onda de Pulso): {pwv} m/s{pwv_status}
"""
        
        # Agregar observaciones solo si existen
        if observaciones and observaciones.strip():
            report_text += f"""
OBSERVACIONES DEL MÉDICO:
  {observaciones}
"""
        
        report_text += """
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        """


        msg.setText(report_text)
        msg.setInformativeText("Este reporte se puede exportar como archivo pdf.")

        font = QFont()
        font.setFamily("Courier New")
        font.setPointSize(10)
        msg.setFont(font)

        # Add custom buttons
        print_btn = msg.addButton("Exportar", QMessageBox.ButtonRole.AcceptRole)
        close_btn = msg.addButton("Cerrar", QMessageBox.ButtonRole.RejectRole)

        print_btn.setStyleSheet("""
            QPushButton {
                background-color: #2196F3;
                color: white;
                font-size: 11pt;
                padding: 8px 16px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #1976D2;
            }
        """)

        close_btn.setStyleSheet("""
            QPushButton {
                background-color: #424242;
                color: white;
                font-size: 11pt;
                padding: 8px 16px;
                border-radius: 5px;
            }
            QPushButton:hover {
                background-color: #616161;
            }
        """)

        msg.exec()

        # If print was clicked, trigger system print dialog
        if msg.clickedButton() == print_btn:
            self.save_pdf(report_text)



    
    def open_date_filter(self):
        dialog = QDialog(self)
        dialog.setWindowTitle("Filtrar por Fecha")
        dialog.setStyleSheet("background-color: #1c1c1c; color: white;")
        dialog.setFixedSize(300, 200)

        layout = QVBoxLayout(dialog)

        # Fecha desde
        from_label = QLabel("Desde:")
        from_date = QDateEdit()
        from_date.setCalendarPopup(True)
        from_date.setDate(QDate.currentDate().addMonths(-1))
        from_date.setStyleSheet("background-color: white; color: black;")

        # Fecha hasta
        to_label = QLabel("Hasta:")
        to_date = QDateEdit()
        to_date.setCalendarPopup(True)
        to_date.setDate(QDate.currentDate())
        to_date.setStyleSheet("background-color: white; color: black;")

        layout.addWidget(from_label)
        layout.addWidget(from_date)
        layout.addWidget(to_label)
        layout.addWidget(to_date)

        # Botones OK / Cancelar
        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok |
            QDialogButtonBox.StandardButton.Cancel
        )
        layout.addWidget(buttons)

        buttons.accepted.connect(
            lambda: self.apply_date_filter(from_date.date(), to_date.date(), dialog)
        )
        buttons.rejected.connect(dialog.reject)

        dialog.exec()

    def apply_date_filter(self, from_date, to_date, dialog):
        filtered_data = []

        for row in self.all_data:
            try:
                # Columna 0 = "Fecha y Hora"
                row_datetime = datetime.strptime(row[0], "%Y-%m-%d %H:%M:%S")
                row_date = QDate(row_datetime.year, row_datetime.month, row_datetime.day)

                if from_date <= row_date <= to_date:
                    filtered_data.append(row)

            except Exception:
                pass

        if not filtered_data:
            QMessageBox.information(
                self,
                "Sin resultados",
                "No hay registros en el rango de fechas seleccionado.\nPor favor seleccione otro rango."
            )
            return  # No cerrar el diálogo, permite seleccionar otro rango

        # Eliminar tabla actual solo si hay resultados
        if hasattr(self, 'table'):
            self.table.setParent(None)
            self.table.deleteLater()

        # Mostrar datos filtrados
        self.show_table(
            ["Fecha y Hora", "Nombre", "Edad", "Altura (cm)", "Sexo", "HR (bpm)", "PWV (m/s)", "Observaciones"],
            filtered_data
        )

        dialog.accept()

# =================================================================================================
# Ventana Principal
# =================================================================================================
class MainScreen(QMainWindow):

    # Layout --------------------------------------------------------------------------------------
    def __init__(self, patient_data):
        super().__init__() # Inicializar la ventana
        
        # Inicializar comunicación (verificar si el método existe)
        if hasattr(ComunicacionMax, 'start'):
            ComunicacionMax.start()
        elif hasattr(ComunicacionMax, 'init'):
            ComunicacionMax.init()
        elif hasattr(ComunicacionMax, 'initialize'):
            ComunicacionMax.initialize()

        # Variables
        self.patient_data = patient_data
        self.measuring = False  # Medición inicialmente desactivada

        try:
            self.patient_age = int(self.patient_data['edad'])
        except (ValueError, KeyError, TypeError):
            self.patient_age = None # Por si acaso

        try:
            altura_m = float(self.patient_data['altura']) / 100.0
            # Verificar si el método existe antes de llamarlo
            if hasattr(processor, 'set_height_from_frontend'):
                processor.set_height_from_frontend(altura_m)
                print("[Frontend] Enviando altura:", altura_m)
            elif hasattr(processor, 'set_height'):
                processor.set_height(altura_m)
                print("[Frontend] Enviando altura:", altura_m)
        except (ValueError, KeyError, AttributeError):
            pass

        self.setWindowTitle("Stiffio") # Título
        self.setGeometry(100, 100, 1400, 800) # Tamaño
        self.central_widget = QWidget()
        self.setCentralWidget(self.central_widget)
        self.central_widget.setStyleSheet("background-color: black; color: white;") # Color de fondo
        self.main_layout = QHBoxLayout() # Layout
        self.central_widget.setLayout(self.main_layout)


        # Columna izquierda
        self.left_layout = QVBoxLayout()
        self.main_layout.addLayout(self.left_layout, 1)

        self.setup_new_button() # Botón Nuevo Paciente
        self.setup_patient_data() # Datos del paciente
        self.setup_results_graph() # Curva PWV vs Edad
        self.setup_control_buttons() # Botones de control
        self.left_layout.addStretch()

        # Columna derecha
        self.right_layout = QVBoxLayout()
        self.main_layout.addLayout(self.right_layout, 2)


        self.title_label = QLabel("Análisis de Rigidez Arterial") # Título Principal
        self.title_label.setStyleSheet("color: white; font-size: 20pt; font-weight: bold;")
        self.title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.right_layout.addWidget(self.title_label)

        self.setup_graphs() # Gráficos
        self.setup_metrics() # Métricas



    # Logo -----------------------------------------------------------
    def setup_new_button(self):
        # Layout horizontal para logo + botón
        top_layout = QHBoxLayout()

        # Logo
        logo_path = "Logo invertido.png"
        if os.path.exists(logo_path):
            self.logo_label = QLabel()
            pixmap = QPixmap(logo_path)
            pixmap = pixmap.scaled(115, 55, Qt.AspectRatioMode.KeepAspectRatio,
                                Qt.TransformationMode.SmoothTransformation)
            self.logo_label.setPixmap(pixmap)
            top_layout.addWidget(self.logo_label, alignment=Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)

        top_layout.addStretch()

        # Botón de guardar medición
        self.save_button = QPushButton("Guardar Medición")
        self.save_button.clicked.connect(self.save_measurement)

        # Botón Nuevo Paciente
        self.new_patient_button = QPushButton("Nuevo Paciente")
        self.new_patient_button.setStyleSheet("""
            QPushButton {
                background-color: #F44336;  /* Rojo */
                color: white;
                font-size: 12pt;
                padding: 10px 20px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #D32F2F;
            }
        """)

        # Conectar acción del botón
        self.new_patient_button.clicked.connect(self.back_to_patient_data)

        top_layout.addSpacing(50)
        top_layout.addWidget(self.new_patient_button, alignment=Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignTop)
        self.left_layout.addLayout(top_layout)



    # Datos del paciente ---------------------------------------------
    def setup_patient_data(self):
        self.patient_data_frame = QWidget()
        self.patient_data_frame.setStyleSheet("""
            background-color: #1c1c1c;
            border-radius: 5px;
            padding: 10px;
        """)
        self.patient_data_layout = QVBoxLayout()
        self.patient_data_frame.setLayout(self.patient_data_layout)
        self.patient_data_frame.setFixedSize(500, 290)
        self.left_layout.addWidget(self.patient_data_frame)

        title_label = QLabel("Datos del paciente") # Título
        title_label.setStyleSheet("color: white; font-size: 20pt; font-weight: bold;")
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.patient_data_layout.addWidget(title_label)


        # Nombre
        self.name_label = QLabel(f"<b>Nombre:</b> {self.patient_data['nombre']}")
        self.name_label.setStyleSheet("color: white; font-size: 16pt;")
        self.patient_data_layout.addWidget(self.name_label)

        # Edad
        self.age_label = QLabel(f"<b>Edad:</b> {self.patient_data['edad']}")
        self.age_label.setStyleSheet("color: white; font-size: 16pt;")
        self.patient_data_layout.addWidget(self.age_label)

        # Altura
        self.height_label = QLabel(f"<b>Altura:</b> {self.patient_data['altura']} cm")
        self.height_label.setStyleSheet("color: white; font-size: 16pt;")
        self.patient_data_layout.addWidget(self.height_label)

        # Sexo
        self.sex_label = QLabel(f"<b>Sexo:</b> {self.patient_data['sexo']}")
        self.sex_label.setStyleSheet("color: white; font-size: 16pt;")
        self.patient_data_layout.addWidget(self.sex_label)


        # Gráfico de Resultados ------------------------------------------
    def setup_results_graph(self):
        self.results_frame = QWidget()
        self.results_frame.setStyleSheet("""
            background-color: #1c1c1c;
            border-radius: 5px;
            padding: 10px;
        """)
        self.left_layout.addWidget(self.results_frame)

        self.results_layout = QVBoxLayout()
        self.results_frame.setLayout(self.results_layout)
        self.results_frame.setFixedSize(500, 340)

        # --- Crear gráfico PWV vs Edad ---
        self.pwv_graph = pg.PlotWidget()
        self.pwv_graph.setBackground('k')
        self.pwv_graph.setTitle("Relación entre Edad y PWV", color='w', size='12pt')
        self.pwv_graph.showGrid(x=True, y=True)
        self.pwv_graph.setLabel('left', 'PWV (m/s)', color='w', size='10pt')
        self.pwv_graph.setLabel('bottom', 'Edad (años)', color='w', size='10pt')

        # --- Datos de la tabla (paper DOI:10.1155/2014/653239) ---
        age_groups = np.array([15, 25, 35, 45, 55, 65, 75])
        mean_pwv = np.array([5.04, 5.86, 6.32, 6.85, 8.15, 8.47, 9.01])
        lower_range = np.array([3.12, 3.92, 4.08, 5.00, 5.46, 6.46, 5.52])
        upper_range = np.array([7.33, 8.14, 8.26, 9.84, 12.50, 11.20, 13.40])

        # --- Ajuste lineal (rectas) ---
        coef_mean = np.polyfit(age_groups, mean_pwv, 1)
        coef_lower = np.polyfit(age_groups, lower_range, 1)
        coef_upper = np.polyfit(age_groups, upper_range, 1)

        poly_mean = np.poly1d(coef_mean)
        poly_lower = np.poly1d(coef_lower)
        poly_upper = np.poly1d(coef_upper)

        # --- Eje continuo para graficar rectas ---
        x_fit = np.linspace(10, 80, 300)
        y_mean = poly_mean(x_fit)
        y_lower = poly_lower(x_fit)
        y_upper = poly_upper(x_fit)

        # --- Crear PlotDataItems para límites (NECESARIO para FillBetweenItem) ---
        upper_item = pg.PlotDataItem(x_fit, y_upper, pen=pg.mkPen((255, 80, 80), width=1.3))
        lower_item = pg.PlotDataItem(x_fit, y_lower, pen=pg.mkPen((255, 80, 80), width=1.3))

        # Añadir los PlotDataItems al gráfico
        self.pwv_graph.addItem(upper_item)
        self.pwv_graph.addItem(lower_item)

        # --- Rellenar área entre límites con verde translúcido ---
        # brush: (R, G, B, Alpha) con Alpha 0-255
        #fill = pg.FillBetweenItem(upper_item, lower_item, brush=pg.mkBrush(220, 237, 200, 70))
        fill = pg.FillBetweenItem(upper_item, lower_item, brush=pg.mkBrush(0, 255, 0, 70))
        self.pwv_graph.addItem(fill)

        # --- Dibujar línea de tendencia (media) encima del relleno ---
        mean_item = pg.PlotDataItem(x_fit, y_mean, pen=pg.mkPen((0, 128, 0), width=2))
        self.pwv_graph.addItem(mean_item)

        # --- Texto R² ---
        #r_value = 0.0616
        #label = pg.TextItem(f"R² lineal = {r_value:.4f}", color='w', anchor=(1, 1))
        # Ajusta la posición si se superpone con el eje
        #label.setPos(75, np.min(y_lower) + 0.5)
        #self.pwv_graph.addItem(label)

        # Crea un item de gráfico de dispersión para el punto del paciente
        # Será un punto grande con blanco.
        self.patient_point_item = pg.ScatterPlotItem(
            size=15,
            brush=pg.mkBrush(255, 255, 255, 255), # Blanco
            pen=pg.mkPen('w', width=2),           # Borde Blanco
            pxMode=True # Asegura que el tamaño sea en píxeles
        )
        self.pwv_graph.addItem(self.patient_point_item)

        # --- Agregar al layout ---
        self.results_layout.addWidget(self.pwv_graph)
        self.results_layout.addSpacing(20)



    # Botones --------------------------------------------------------
    def setup_control_buttons(self):
        # Layout horizontal para los botones
        self.graph_buttons_layout = QHBoxLayout()
        self.left_layout.addSpacing(20)

        self.start_graph_button = QPushButton("Iniciar Medición")
        self.start_graph_button.setFixedSize(207, 61)
        self.start_graph_button.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                font-size: 12pt;
                padding: 10px 30px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
        """)

        self.start_graph_button.clicked.connect(self.toggle_measurement) # Clic para iniciar medición


        self.save_graph_button = QPushButton("Guardar Medición")
        self.save_graph_button.setFixedSize(207, 61)
        self.save_graph_button.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                font-size: 12pt;
                padding: 10px 30px;
                border-radius: 5px;
                font-weight: bold;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
        """)

        self.save_graph_button.setMinimumHeight(50)

        self.save_graph_button.clicked.connect(self.save_measurement) # Clic para guardar medición

        self.graph_buttons_layout.addWidget(self.start_graph_button)
        self.graph_buttons_layout.addWidget(self.save_graph_button)
        self.left_layout.addLayout(self.graph_buttons_layout)


    # Gráficos Señales -----------------------------------------------
    def setup_graphs(self):

        # Gráfico sensor 1 (proximal)
        self.graph1 = pg.PlotWidget()
        self.graph1.setBackground('k')
        self.graph1.setTitle("Sensor Proximal (Carótida)", color='w', size='12pt')
        self.graph1.showGrid(x=True, y=True)
        self.graph1.setLabel('left', 'Amplitud (%)')
        self.graph1.setLabel('bottom', 'Tiempo (s)')
        self.right_layout.addWidget(self.graph1)

        # Gráfico sensor 2 (distal)
        self.graph2 = pg.PlotWidget()
        self.graph2.setBackground('k')
        self.graph2.setTitle("Sensor Distal (Radial)", color='w', size='12pt')
        self.graph2.showGrid(x=True, y=True)
        self.graph2.setLabel('left', 'Amplitud (%)')
        self.graph2.setLabel('bottom', 'Tiempo (s)')
        self.right_layout.addWidget(self.graph2)

        # Curvas de datos
        self.curve1 = self.graph1.plot([], [], pen=pg.mkPen('red', width=2))
        self.curve2 = self.graph2.plot([], [], pen=pg.mkPen('pink', width=2))

        # Alerta para sensor 1 (proximal)
        self.prox_alert_label = QLabel("REVISAR SENSOR")
        self.prox_alert_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.prox_alert_label.setStyleSheet("""
            background-color: red;
            color: white;
            font-size: 22pt;
            font-weight: bold;
            border-radius: 10px;
        """)
        self.prox_alert_label.setVisible(False)  # No mostrar al principio
        # Hacer que ocupe todo el PlotWidget
        self.prox_alert_label.setParent(self.graph1)
        self.prox_alert_label.setGeometry(270, 110, 500, 100) # Posición y tamaño del cartel

        # Alerta para sensor 2 (distal)
        self.dist_alert_label = QLabel("REVISAR SENSOR")
        self.dist_alert_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.dist_alert_label.setStyleSheet("""
            background-color: pink;
            color: white;
            font-size: 22pt;
            font-weight: bold;
            border-radius: 10px;
        """)
        self.dist_alert_label.setVisible(False) # No mostrar al principio
        self.dist_alert_label.setParent(self.graph2)
        self.dist_alert_label.setGeometry(270, 110, 500, 100) # Posición y tamaño del cartel


    # Métricas -------------------------------------------------------
    def setup_metrics(self):
        self.metrics_layout = QHBoxLayout()
        self.right_layout.addLayout(self.metrics_layout)

        # PWV
        self.pwv_label = QLabel("PWV: -- m/s")
        self.pwv_label.setStyleSheet("""
            color: #d4a017;
            font-size: 18pt;
            padding: 15px;
            background-color: #1c1c1c;
            border-radius: 5px;
            font-weight: bold;
        """)
        self.metrics_layout.addWidget(self.pwv_label)

        # HR
        self.hr_esp_label = QLabel("HR: -- bpm")
        self.hr_esp_label.setStyleSheet("""
            color: red;
            font-size: 18pt;
            padding: 15px;
            background-color: #1c1c1c;
            border-radius: 5px;
            font-weight: bold;
        """)
        self.metrics_layout.addWidget(self.hr_esp_label)


    # Funcionalidad -------------------------------------------------------------------------------

    # Realizar una medición con nuevo paciente
    def back_to_patient_data(self):

        # Limpiamos el buffer y el valor de PWV del backend
        # para que el nuevo paciente comience de cero.
        processor.pwv_buffer.clear()
        processor.pwv = None

        from __main__ import PatientDataScreen
        self.patient_data_window = PatientDataScreen()

        # Mantener tamaño y posición de la ventana actual
        self.patient_data_window.resize(self.size())
        self.patient_data_window.move(self.pos())
        if self.isMaximized():
            self.patient_data_window.showMaximized()
        else:
            self.patient_data_window.show()

        # Cerrar la ventana actual después de mostrar la nueva
        QTimer.singleShot(50, self.close)

    # Iniciar medición
    def toggle_measurement(self):
        if not self.measuring:
            # =======================================================
            # BLOQUE NUEVO: ENVIAR DATOS AL ESP32
            # =======================================================
            try:
                # Recuperamos los datos que vienen del diccionario
                h_val = int(self.patient_data.get('altura', 170))
                a_val = int(self.patient_data.get('edad', 30))
                
                # Enviamos al ESP32
                if ComunicacionMax.connected:
                    ComunicacionMax.enviar_datos_paciente(h_val, a_val)
                    print(f"[FrontEnd] Datos enviados a ESP32: Altura {h_val}, Edad {a_val}")
                else:
                    print("[FrontEnd] ESP32 no conectado (Modo Offline o Error)")
            except Exception as e:
                print(f"[FrontEnd] Error enviando datos: {e}")
            # =======================================================

            # --- Iniciar medición (Código original tuyo) ---
            self.measuring = True
            self.start_time = time.time()
            self.start_graph_button.setText("Detener Medición")
            self.start_graph_button.setStyleSheet("""
                QPushButton {
                    background-color: #F44336;  /* Rojo */
                    color: white;
                    font-size: 12pt;
                    padding: 10px 30px;
                    border-radius: 5px;
                    font-weight: bold;
                }
                QPushButton:hover {
                    background-color: #D32F2F;
                }
            """)
            self.start_graph_update() # <--- Esto inicia el Timer
        else:
            # --- Detener medición (Código original tuyo) ---
            self.measuring = False
            self.start_graph_button.setText("Iniciar Medición")
            self.start_graph_button.setStyleSheet("""
                QPushButton {
                    background-color: #4CAF50;  /* Verde */
                    color: white;
                    font-size: 12pt;
                    padding: 10px 30px;
                    border-radius: 5px;
                    font-weight: bold;
                }
                QPushButton:hover {
                    background-color: #45a049;
                }
            """)
            self.stop_graph_update()

    # Arranca el gráfico
    def start_graph_update(self):
        self.timer = QTimer()
        self.timer.timeout.connect(self.update_plot)
        self.timer.start(100)  # 10 Hz (actualiza la gráfica cada 100 ms)

    # Detiene el gráfico
    def stop_graph_update(self):
        if hasattr(self, 'timer'):
            self.timer.stop()


    # Actualiza el gráfico
    def update_plot(self):
        if not self.measuring:
            return

        # Leer señales desde ComunicacionMax
        y1 = list(ComunicacionMax.proximal_data_raw)
        y2 = list(ComunicacionMax.distal_data_raw)

        # Verificar estado de sensores
        s1_ok = ComunicacionMax.sensor1_ok
        s2_ok = ComunicacionMax.sensor2_ok

        # Actualizar visibilidad de alertas
        self.prox_alert_label.setVisible(not s1_ok)
        self.dist_alert_label.setVisible(not s2_ok)

        # Eje temporal
        n = len(y1)
        elapsed_time = time.time() - self.start_time
        t_end = elapsed_time
        t_start = max(0, t_end - 6)
        t = np.linspace(t_start, t_end, n)

        # Si algún sensor no está apoyado, vaciar gráficos y poner métricas en "--"
        if not (s1_ok and s2_ok):
            self.curve1.setData([])
            self.curve2.setData([])
            self.hr_esp_label.setText("HR: -- bpm")
            self.pwv_label.setText("PWV: -- m/s")
            return
        elif not s1_ok:
            self.curve1.setData([])
            self.curve2.setData(y2)  # Solo distal
            self.hr_esp_label.setText("HR: -- bpm")
            self.pwv_label.setText("PWV: -- m/s")
            return
        elif not s2_ok:
            self.curve1.setData(y1)  # Solo proximal
            self.curve2.setData([])
            self.hr_esp_label.setText("HR: -- bpm")
            self.pwv_label.setText("PWV: -- m/s")
            return


        # Actualizar gráficos
        if s1_ok:
            self.curve1.setData(t, y1)
            self.graph1.setXRange(t_start, t_end)
            self.graph1.setYRange(-0.1, 0.1)
        else:
            self.curve1.setData([])


        if s2_ok:
            self.curve2.setData(t, y2)
            self.graph2.setXRange(t_start, t_end)
            self.graph2.setYRange(-0.1, 0.1)
        else:
            self.curve2.setData([])


        # Actualizar HR promedio si está disponible
        if ComunicacionMax.hr_avg is not None:
            self.hr_esp_label.setText(f"HR: {ComunicacionMax.hr_avg:.0f} bpm")
        else:
            self.hr_esp_label.setText("HR: -- bpm")

        # Calcular y actualizar la PWV
        processor.process_all()
        pwv = processor.get_metrics().get('pwv')

        if pwv is not None:
            # Si hay un valor de PWV (ya promediado y estable)...
            self.pwv_label.setText(f"PWV: {pwv:.1f} m/s")

            if self.patient_age is not None:
                # Actualizamos la posición del punto amarillo
                self.patient_point_item.setData([self.patient_age], [pwv])
        else:
            # Si no hay PWV (aún calibrando), mostramos "--"
            self.pwv_label.setText("PWV: -- m/s")

    # ... (tus otras funciones como setup_ui, update_plot, etc.) ...

    # Guardar medición
    def save_measurement(self):
        filename = "mediciones_pwv.csv"

        # 1. Obtener los valores actuales
        # (Usamos .get() para evitar errores si una clave no existe)
        nombre = self.patient_data.get('nombre', 'N/A')
        edad = self.patient_data.get('edad', 'N/A')
        altura = self.patient_data.get('altura', 'N/A')
        sexo = self.patient_data.get('sexo', 'N/A')
        observaciones = self.patient_data.get('observaciones', '')

        # Obtener métricas del backend (los valores ya promediados)
        pwv_val = processor.get_metrics().get('pwv')
        hr_val = ComunicacionMax.hr_avg # HR del .ino

        # 2. Validar que tengamos datos
        if pwv_val is None or hr_val is None:
            QMessageBox.warning(self, "Datos Incompletos",
                                "No se puede guardar la medición.\n"
                                "Asegúrese de que la PWV y la HR se estén midiendo.")
            return

        # 3. Formatear los datos
        pwv_str = f"{pwv_val:.1f}"
        hr_str = f"{hr_val:.0f}"
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # 4. Preparar la fila y el encabezado (observaciones al final)
        header = ["Fecha y Hora", "Nombre", "Edad", "Altura (cm)", "Sexo", "HR (bpm)", "PWV (m/s)", "Observaciones"]
        data_row = [timestamp, nombre, edad, altura, sexo, hr_str, pwv_str, observaciones]

        # 5. Escribir en el archivo CSV
        try:
            # Revisar si el archivo ya existe para no escribir el encabezado cada vez
            file_exists = os.path.isfile(filename)

            # Usamos 'a' (append) para añadir al final sin borrar lo anterior
            with open(filename, mode='a', newline='', encoding='utf-8-sig') as file:
                writer = csv.writer(file, delimiter=';')

                if not file_exists:
                    writer.writerow(header)  # Escribir encabezado si el archivo es nuevo

                writer.writerow(data_row) # Escribir la fila de datos

            # 6. Mostrar mensaje de éxito
            QMessageBox.information(self, "Guardado Exitoso",
                                    f"Medición guardada en:\n{filename}")

        except Exception as e:
            # 7. Mostrar mensaje de error
            QMessageBox.critical(self, "Error al Guardar",
                                 f"No se pudo guardar el archivo:\n{e}")


# =================================================================================================
# Loop Principal
# =================================================================================================
import sys
if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = WelcomeScreen()
    window.showMaximized()  # Esto hace que abra maximizada
    sys.exit(app.exec())
exit(app.exec())