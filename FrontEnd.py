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
import math
import csv
from reportlab.lib.pagesizes import A4
from reportlab.pdfgen import canvas
from datetime import datetime

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QHBoxLayout, QVBoxLayout,
    QLabel, QLineEdit, QPushButton, QComboBox, QStatusBar, QMessageBox, QDialog, QDateEdit, QDialogButtonBox, 
    QFileDialog, QTextEdit, QAbstractItemView)
from PyQt6.QtCore import QTimer, Qt, QRegularExpression, QDate, QSize
from PyQt6.QtGui import QPixmap, QRegularExpressionValidator, QFont, QIcon, QTextDocument, QIntValidator

from pyqtgraph import FillBetweenItem

from PyQt6.QtWidgets import QTableWidget, QTableWidgetItem

# Importar el backend y la comunicación
from BackEnd import processor
import ComunicacionMax

from PyQt6.QtPrintSupport import QPrinter

def resource_path(relative_path):
    """Obtiene la ruta correcta tanto en desarrollo como en ejecutable"""
    try:
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    return os.path.join(base_path, relative_path)
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
        logo_path = resource_path("Logo invertido.png")
        if os.path.exists(logo_path):
            logo_label = QLabel()
            pixmap = QPixmap(resource_path(logo_path))
            pixmap = pixmap.scaled(700, 250, Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation) # Tamaño
            logo_label.setPixmap(pixmap)
            logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter) # Posición
            layout.addWidget(logo_label)

        # Texto -----------------------------------------------------------
        text_label = QLabel("Sistema de Medición de Velocidad de Onda de Pulso")
        text_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        text_label.setStyleSheet("font-size: 16pt; font-weight: bold;")
        layout.addWidget(text_label)
        layout.addSpacing(50)

        # Botón comenzar -----------------------------------------------------------
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

        layout.addSpacing(20)

        # Botón historial  -----------------------------------------------------------
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
        logo_path = resource_path("Logo invertido.png")
        if os.path.exists(logo_path):
            logo_label = QLabel()
            pixmap = QPixmap(resource_path(logo_path))
            pixmap = pixmap.scaled(100,50, Qt.AspectRatioMode.KeepAspectRatio,
                                   Qt.TransformationMode.SmoothTransformation)  # Tamaño
            logo_label.setPixmap(pixmap)
            logo_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
            self.layout.addWidget(logo_label)
            self.layout.addSpacing(30)


        # Título ----------------------------------------------------------
        title_label = QLabel("Datos del paciente")
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title_label.setStyleSheet("color: white; font-size: 24pt; font-weight: bold;")
        self.layout.addWidget(title_label)
        self.layout.addSpacing(30)


        # Inputs ----------------------------------------------------------

        # --- FILA 1: Nombre y Apellido ---
        row1_layout = QHBoxLayout()

        # Nombre
        self.name_container = QVBoxLayout()
        self.name_label = QLabel("Nombre")
        self.name_label.setStyleSheet("color: white; font-size: 14pt;")
        self.name_input = QLineEdit()
        self.name_input.setStyleSheet("background-color: white; color: black; font-size: 14pt; padding: 5px;")
        self.name_input.setFixedWidth(365)
        self.name_container.addWidget(self.name_label)
        self.name_container.addWidget(self.name_input)
        
        
        # Apellido
        self.surname_container = QVBoxLayout()
        self.surname_label = QLabel("Apellido")
        self.surname_label.setStyleSheet("color: white; font-size: 14pt;")
        self.surname_input = QLineEdit()
        self.surname_input.setStyleSheet("background-color: white; color: black; font-size: 14pt; padding: 5px;")
        self.surname_input.setFixedWidth(365)
        self.surname_container.addWidget(self.surname_label)
        self.surname_container.addWidget(self.surname_input)
        
        row1_layout.addLayout(self.name_container)
        row1_layout.addSpacing(20)
        row1_layout.addLayout(self.surname_container)
        self.layout.addLayout(row1_layout)
        self.layout.addSpacing(20)


        # --- FILA 2: DNI  ---
        self.dni_label = QLabel("DNI")
        self.dni_label.setStyleSheet("color: white; font-size: 14pt;")
        self.dni_input = QLineEdit()
        self.dni_input.setStyleSheet("background-color: white; color: black; font-size: 14pt; padding: 5px;")
        self.dni_input.setMaxLength(8)
        self.dni_input.setValidator(QIntValidator()) # Solo números
        self.dni_input.setFixedWidth(750)
        self.layout.addWidget(self.dni_label)
        self.layout.addWidget(self.dni_input, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addSpacing(20)


        # --- FILA 3: Edad, Altura y Sexo ---
        row3_layout = QHBoxLayout()
        
        # Edad
        self.age_container = QVBoxLayout()
        self.age_label = QLabel("Edad")
        self.age_label.setStyleSheet("color: white; font-size: 14pt;")
        self.age_input = QLineEdit()
        self.age_input.setFixedWidth(230) # Limitar ancho
        self.age_input.setStyleSheet("background-color: white; color: black; font-size: 14pt; padding: 5px;")
        self.age_input.setMaxLength(3)
        self.age_input.setValidator(QRegularExpressionValidator(QRegularExpression(r"^[0-9]{1,3}$")))
        self.age_container.addWidget(self.age_label)
        self.age_container.addWidget(self.age_input)

        # Altura
        self.height_container = QVBoxLayout()
        self.height_label = QLabel("Altura (cm)")
        self.height_label.setStyleSheet("color: white; font-size: 14pt;")
        self.height_input = QLineEdit()
        self.height_input.setFixedWidth(230) # Limitar ancho
        self.height_input.setStyleSheet("background-color: white; color: black; font-size: 14pt; padding: 5px;")
        self.height_input.setMaxLength(3)
        self.height_input.setValidator(QRegularExpressionValidator(QRegularExpression(r"^[0-9]{2,3}$")))
        self.height_container.addWidget(self.height_label)
        self.height_container.addWidget(self.height_input)

        # Sexo
        self.sex_container = QVBoxLayout()
        self.sex_label = QLabel("Sexo")
        self.sex_label.setStyleSheet("color: white; font-size: 14pt;")
        self.sex_combo = QComboBox()
        self.sex_combo.setFixedWidth(230) # Limitar ancho
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

        self.sex_container.addWidget(self.sex_label)
        self.sex_container.addWidget(self.sex_combo)
    
        # Unir todo en la fila horizontal
        row3_layout.addLayout(self.age_container)
        row3_layout.addSpacing(20)
        row3_layout.addLayout(self.height_container)
        row3_layout.addSpacing(20)
        row3_layout.addLayout(self.sex_container)
        
        self.layout.addLayout(row3_layout)
        self.layout.addSpacing(30)


        # --- FILA 4: Observaciones ---

        # Observaciones (opcional)
        self.observations_label = QLabel("Observaciones")
        self.observations_label.setStyleSheet("color: white; font-size: 14pt;")
        self.layout.addWidget(self.observations_label)

        self.observations_input = QTextEdit()
        self.observations_input.setStyleSheet("background-color: white; color: black; font-size: 10pt; padding: 5px;")
        self.observations_input.setFixedWidth(750)
        self.observations_input.setFixedHeight(100)
        self.observations_input.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOn) # Evita que el texto expanda el widget
        self.layout.addWidget(self.observations_input, alignment=Qt.AlignmentFlag.AlignCenter)
        self.layout.addSpacing(60)


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
        surname_ok = bool(self.surname_input.text().strip())
        dni_ok = bool(self.dni_input.text().strip())
        age_ok = bool(self.age_input.text().strip())
        height_ok = bool(self.height_input.text().strip())
        sex_ok = self.sex_combo.currentText() != ""

        if not (name_ok and surname_ok and dni_ok and age_ok and height_ok and sex_ok):
            msg = QMessageBox(self)
            msg.setIcon(QMessageBox.Icon.Warning)
            msg.setWindowTitle("Campos incompletos")
            msg.setText("Por favor, complete todos los datos antes de continuar.")

            msg.setStyleSheet("""
                QMessageBox { 
                    background-color: white; 
                }
                QLabel { 
                    color: black; 
                    background-color: transparent; 
                }
            """)

            font = QFont()
            font.setPointSize(14)  # Tamaño
            msg.setFont(font)

            ok_button = msg.addButton(QMessageBox.StandardButton.Ok) # Botón OK
            ok_button.setStyleSheet("""
                QPushButton {
                    background-color: #424242;
                    color: white;
                    font-size: 12pt;
                    padding: 8px 16px;
                    border-radius: 5px;
                }
                QPushButton:hover {
                    background-color: #616161;
                }
            """)

            msg.exec()  # Mostrar la alerta
            return  # Salir de la función, no abrir la siguiente ventana


           # ---------- Validación de edad ----------
        try:
            age_val = int(self.age_input.text())
            if not (10 <= age_val <= 100):
                raise ValueError
        except ValueError:
            msg = QMessageBox(self)
            msg.setIcon(QMessageBox.Icon.Warning)
            msg.setWindowTitle("Edad inválida")
            msg.setText("La edad ingresada es inválida.\nDebe estar entre 10 y 100 años.")
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
            "apellido": self.surname_input.text(),
            "dni": self.dni_input.text(),
            "edad": self.age_input.text(),
            "altura": self.height_input.text(),
            "sexo": self.sex_combo.currentText(),
            "observaciones": self.observations_input.toPlainText().strip()
        }

        try:
            
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



# =================================================================================================
# Ventana de Historial de Mediciones
# =================================================================================================
class HistoryScreen(QWidget):
    def save_pdf(self, report_text):

        # ================= SELECCIÓN DE RUTA =================
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

        # ================= CREAR PDF =================
        c = canvas.Canvas(file_path, pagesize=A4)
        width, height = A4

        # ================= LOGO =================
        logo_path = resource_path("Logo.jpg")
        if os.path.exists(logo_path):
            logo_width = 140
            logo_height = 60
            x_centered = (width - logo_width) / 2

            c.drawImage(
                logo_path,
                x=x_centered,
                y=height - 100,
                width=logo_width,
                height=logo_height,
                preserveAspectRatio=True,
                mask='auto'
            )

        # ================= TÍTULO =================
        c.setFont("Helvetica-Bold", 16)
        c.drawCentredString(width / 2, height - 160, "REPORTE DE MEDICIÓN")

        # ================= TEXTO =================
        margen_izquierdo = 140
        text = c.beginText(margen_izquierdo, height - 200)
        text.setFont("Helvetica", 11)
        text.setLeading(14)

        for line in report_text.strip().split("\n"):
            text.textLine(line)

        c.drawText(text)

        c.showPage()
        c.save()

        # ================= MENSAJE DE CONFIRMACIÓN =================
        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Icon.Information)
        msg.setWindowTitle("Descarga completa")
        msg.setText("Reporte Exportado")
        msg.setInformativeText(f"El archivo se guardó correctamente en:\n{file_path}")

        msg.setStyleSheet("""
            QMessageBox {
                background-color: white; 
            }
            QLabel {
                color: black;
                background-color: transparent;
                font-family: 'Segoe UI';
                font-size: 10pt;
            }  
            QPushButton {
                background-color: #424242;
                color: white;
                font-size: 10pt;
                padding: 8px 16px;
                border-radius: 5px;
            }
            QPushButton:hover {
                background-color: #616161;
            }
        """)

        msg.exec()


    def __init__(self):
        super().__init__()
        self.setWindowTitle("Historial de Mediciones")
        self.setStyleSheet("background-color: black; color: white;")

        self.layout = QVBoxLayout()
        self.setLayout(self.layout)

        logo_path = resource_path("Logo invertido.png")
        if os.path.exists(logo_path):
            logo_label = QLabel()
            pixmap = QPixmap(resource_path(logo_path))
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
        self.search_input.setPlaceholderText("Buscar por DNI, Nombre o Apellido...")
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
                font-size: 12pt;
                padding: 10px 20px;
                border-radius: 5px;
                font-weight: bold;
                max-width: 200px;
            }
            QPushButton:hover {
                background-color: #D32F2F;
            }
        """)
        back_button.clicked.connect(self.go_back)
        self.layout.addWidget(back_button, alignment=Qt.AlignmentFlag.AlignLeft)


    @staticmethod
    def _crpwv_bounds_for_age(age_years):
        exp_term = np.exp(0.0022 * age_years)
        upper = 12.0 * exp_term
        lower = 4.0 * exp_term
        return lower, upper


    def _is_crpwv_normal(self, age_years, crpwv_value):
        lower, upper = self._crpwv_bounds_for_age(age_years)
        return lower <= crpwv_value <= upper




    def load_data(self):
        filename = resource_path("mediciones_pwv.csv") 
        self.all_data = [] # Inicializamos para evitar el AttributeError

        # Si el archivo NO existe, lo creamos con el formato correcto vacio
        if not os.path.exists(filename):
            print(f"Creando archivo nuevo: {filename}")
            header = ["Fecha y Hora", "DNI", "Nombre", "Apellido", "Edad", "Altura (cm)", "Sexo", "HR (bpm)", "crPWV (m/s)", "Observaciones"]

            try:
                with open(filename, mode='w', newline='', encoding='utf-8-sig') as file:
                    writer = csv.writer(file, delimiter=';')
                    writer.writerow(header)
            except Exception as e:
                print(f"Error al crear el archivo: {e}")

        # Una vez creado (o si ya existía), lo cargamos normalmente
        try:
            with open(filename, newline='', encoding='utf-8-sig') as file:
                rows = list(csv.reader(file, delimiter=';'))

            if len(rows) > 1:
                # Filtramos posibles filas vacías al final
                self.all_data = [r for r in rows[1:] if len(r) >= 9]
                self.show_table(self.all_data)
            else:
                self.show_empty_message()

        except Exception as e:
            print(f"Error al leer datos: {e}")
            self.show_empty_message()

    def show_empty_message(self):
        label = QLabel("No existen registros.")
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        label.setStyleSheet("font-size: 18pt; color: gray;")
        self.layout.addWidget(label)

    def show_table(self, data):
        self.table = QTableWidget()
        self.table.setRowCount(len(data))
        visible_headers = ["Fecha y Hora", "DNI", "Nombre",  "Apellido", "Edad", "Altura", "Sexo", "HR", "crPWV", "Acciones"]
        self.table.setColumnCount(len(visible_headers))
        self.table.setHorizontalHeaderLabels(visible_headers)

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

        self.table.setAlternatingRowColors(False)
        self.table.verticalHeader().setVisible(False)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setVerticalScrollMode(QAbstractItemView.ScrollMode.ScrollPerPixel) # Barra deslizante
        self.table.verticalScrollBar().setSingleStep(5)

        for r, row in enumerate(data):
            # 0:Fecha, 1:DNI, 2:Nombre, 3:Apellido, 4:Edad, 5:Altura, 6:Sexo, 7:HR, 8:crPWV
            
            # Fecha (DD/MM/YYYY HH:MM)
            try:
                raw_date = row[0]
                # Leemos el formato del CSV (Año-Mes-Día)
                dt_obj = datetime.strptime(raw_date, "%Y-%m-%d %H:%M:%S")
                
                # Lo escribimos en formato local (Día/Mes/Año) y sin segundos
                clean_date = dt_obj.strftime("%d/%m/%Y %H:%M")
                
                self.table.setItem(r, 0, QTableWidgetItem(clean_date))
            except Exception:
                self.table.setItem(r, 0, QTableWidgetItem(row[0]))
             
            self.table.setItem(r, 1, QTableWidgetItem(row[1])) # DNI
            self.table.setItem(r, 2, QTableWidgetItem(row[2])) # Nombre
            self.table.setItem(r, 3, QTableWidgetItem(row[3])) # Apellido
            self.table.setItem(r, 4, QTableWidgetItem(row[4])) # Edad
            self.table.setItem(r, 5, QTableWidgetItem(row[5])) # Altura
            self.table.setItem(r, 6, QTableWidgetItem(row[6])) # Sexo
            self.table.setItem(r, 7, QTableWidgetItem(row[7])) # HR

            # crPWV con lógica dinámica por edad (zona fisiológica del gráfico de referencia)
            pwv_item = QTableWidgetItem(row[8])
            try:
                val = float(row[8])
                edad_val = float(row[4])
                es_normal = self._is_crpwv_normal(edad_val, val)
                pwv_item.setForeground(Qt.GlobalColor.green if es_normal else Qt.GlobalColor.red)
            except: pass
            self.table.setItem(r, 8, pwv_item)

            # Botones
            actions_widget = QWidget()
            actions_widget.setStyleSheet("background-color: transparent;") 
            actions_layout = QHBoxLayout(actions_widget)
            actions_layout.setContentsMargins(5, 2, 5, 2)
            actions_layout.setSpacing(8)

            # Boton de descargar
            download_button = QPushButton()
            download_button.setIcon(QIcon(resource_path("download-icon.png")))
            download_button.setIconSize(QSize(35, 35))
            download_button.setFixedSize(50, 50)
            download_button.setStyleSheet("""
                QPushButton {
                    background-color: transparent;
                    border: none;
                }
                QPushButton:hover {
                    background-color: rgba(255, 255, 255, 0.1);
                }
            """)
            download_button.clicked.connect(lambda checked, row=r: self.print_record(row))
            actions_layout.addWidget(download_button)

            # Boton de eliminar
            delete_button = QPushButton()
            delete_button.setIcon(QIcon(resource_path("delete-icon.png")))
            delete_button.setIconSize(QSize(35, 35))
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

            self.table.setCellWidget(r, 9, actions_widget)
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

        # Mensaje de confirmación
        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Icon.Warning)
        msg.setWindowTitle("Confirmar Eliminación")
        msg.setText(f"¿Está seguro que desea eliminar este registro?")
        msg.setInformativeText("Esta acción no se puede deshacer.")

        font = QFont()
        font.setPointSize(12)
        msg.setFont(font)

        # Botones personalizados
        yes_button = msg.addButton("Eliminar", QMessageBox.ButtonRole.YesRole)
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
        filename = resource_path("mediciones_pwv.csv")

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
       
        self.welcome = WelcomeScreen()
        self.welcome.resize(self.size())
        self.welcome.move(self.pos())
        if self.isMaximized():
            self.welcome.showMaximized()
        else:
            self.welcome.show()
        QTimer.singleShot(50, self.close)


    def print_record(self, row):
        # Extraer datos de las celdas según los nuevos índices de la tabla
        fecha_hora = self.table.item(row, 0).text()
        dni = self.table.item(row, 1).text()
        nombre = self.table.item(row, 2).text()
        apellido = self.table.item(row, 3).text()
        edad = self.table.item(row, 4).text()
        altura = self.table.item(row, 5).text()
        sexo = self.table.item(row, 6).text()
        hr = self.table.item(row, 7).text()
        pwv = self.table.item(row, 8).text()

        # Obtener observaciones (No visibles en tabla, se buscan en la lista original)
        observaciones = ""
        for r in self.all_data:
            if r[1] == dni: # La columna 1 en el CSV es el DNI
                observaciones = r[9] # La columna 9 son las observaciones
                break

        # Determinamos el estado para el reporte con criterio dinámico por edad
        pwv_status = ""
        try:
            pwv_val = float(pwv.split()[0]) # .split() por si tiene el " m/s"
            edad_val = float(edad)
            es_normal = self._is_crpwv_normal(edad_val, pwv_val)
            pwv_status = " (Normal)" if es_normal else " (Anormal)"
        except: pass

        # Construir el texto del reporte
        report_text = f"""

Fecha y Hora: {fecha_hora}
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

DATOS DEL PACIENTE:
  • Nombre y Apellido: {nombre} {apellido}
  • DNI: {dni}
  • Edad: {edad}
  • Altura: {altura}
  • Sexo: {sexo}

  
RESULTADOS DE LA MEDICIÓN:
  • HR: {hr} bpm
  • crPWV: {pwv} m/s{pwv_status}

"""
        # Agregar observaciones solo si existen
        if observaciones and observaciones.strip():
            report_text += f"""
OBSERVACIONES:
  {observaciones}
"""
        
        report_text += """
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        """

        msg = QMessageBox(self) 
        msg.setIcon(QMessageBox.Icon.Information)
        msg.setWindowTitle("Previsualización de Reporte")
        
        # Estilo Stiffio: Fondo blanco y texto negro para legibilidad
        msg.setStyleSheet("""
            QMessageBox { background-color: white; }
            QLabel { color: black; background-color: transparent; }
        """)

        msg.setText(report_text)
    

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
            msg = QMessageBox(self)
            msg.setIcon(QMessageBox.Icon.Information)
            msg.setWindowTitle("Sin resultados")
            msg.setText("No hay registros en el rango de fechas seleccionado.")
            msg.setStyleSheet("QMessageBox { background-color: white; } QLabel { color: black; }")
            msg.exec()
            return

        # Eliminar tabla actual solo si hay resultados
        if hasattr(self, 'table'):
            self.table.setParent(None)
            self.table.deleteLater()

        # Mostrar datos filtrados
        self.show_table(filtered_data)

        dialog.accept()








# =================================================================================================
# Ventana Principal
# =================================================================================================
class MainScreen(QMainWindow):

    # Layout --------------------------------------------------------------------------------------
    def __init__(self, patient_data):
        super().__init__() # Inicializar la ventana
        
        # Inicializar comunicación (verificar si el método existe)
        try:
            ComunicacionMax.start_connection()
        except Exception:
            pass

        # Variables
        self.patient_data = patient_data
        self._show_calibrating_until_ready = False
        self._curve_hold_seconds = 0.8
        self._last_curve1_data_time = 0.0
        self._last_curve2_data_time = 0.0
        self._last_x_end = None
        self._last_x_range = None
        self._last_valid_hr = None
        self._last_valid_pwv = None
        self._last_plot_seq = -1
        self._last_allow_signal_plot = None
        self._session_started_once = False
        self._patient_data_sent = False
        self._last_patient_data_send_attempt = 0.0
        self._patient_data_retry_sec = 1.0
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
            elif hasattr(processor, 'set_height'):
                processor.set_height(altura_m)
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
        self.setup_results_graph() # Curva crPWV vs Edad
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
        logo_path = resource_path("Logo invertido.png")
        if os.path.exists(logo_path):
            self.logo_label = QLabel()
            pixmap = QPixmap(resource_path(logo_path))
            pixmap = pixmap.scaled(115, 55, Qt.AspectRatioMode.KeepAspectRatio,
                                Qt.TransformationMode.SmoothTransformation)
            self.logo_label.setPixmap(pixmap)
            top_layout.addWidget(self.logo_label, alignment=Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)

        top_layout.addStretch()

        # Botón Salir 
        self.exit_button = QPushButton("Salir")
        self.exit_button.setFixedWidth(160)
        self.exit_button.setStyleSheet("""
            QPushButton {
                background-color: #F44336;
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
        self.exit_button.clicked.connect(self.go_back) 
        top_layout.addWidget(self.exit_button, alignment=Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignTop)


        # Espaciado entre botones
        top_layout.addSpacing(5)


        # Botón de guardar medición
        # self.save_button = QPushButton("Guardar Medición")
        # self.save_button.clicked.connect(self.save_measurement)

        # Botón Nuevo Paciente
        self.new_patient_button = QPushButton("Nuevo Paciente")
        self.new_patient_button.setFixedWidth(160)
        self.new_patient_button.setStyleSheet("""
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

        # Conectar acción del botón
        self.new_patient_button.clicked.connect(self.back_to_patient_data)
        top_layout.addSpacing(20)
        top_layout.addWidget(self.new_patient_button, alignment=Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignTop)
        
        # Añadimos la fila completa al layout de la columna izquierda
        self.left_layout.addLayout(top_layout)



    # Datos del paciente ---------------------------------------------
    def setup_patient_data(self):
        self.patient_data_frame = QWidget()
        self.patient_data_frame.setStyleSheet("""
            background-color: #1c1c1c;
            border-radius: 5px;
            padding: 3px;
        """)
        self.patient_data_layout = QVBoxLayout()
        self.patient_data_layout.setContentsMargins(15, 10, 15, 10) # Márgenes pequeños (L, T, R, B)
        self.patient_data_layout.setSpacing(2) # Espaciado mínimo entre etiquetas
        self.patient_data_frame.setLayout(self.patient_data_layout)
        self.patient_data_frame.setFixedSize(500, 290)
        self.left_layout.addWidget(self.patient_data_frame)
        
        title_label = QLabel("Datos del paciente") # Título
        title_label.setStyleSheet("color: white; font-size: 18pt; font-weight: bold;")
        title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.patient_data_layout.addWidget(title_label)
        self.patient_data_layout.addSpacing(5)


        # Nombre y Apellido
        full_name = f"{self.patient_data.get('nombre', '')} {self.patient_data.get('apellido', '')}"
        self.name_label = QLabel(f"<b>Nombre y Apellido:</b> {full_name}")
        self.name_label.setStyleSheet("color: white; font-size: 14pt;")
        self.patient_data_layout.addWidget(self.name_label)

        # DNI
        dni_val = self.patient_data.get('dni', 'N/A')
        self.dni_label = QLabel(f"<b>DNI:</b> {dni_val}")
        self.dni_label.setStyleSheet("color: white; font-size: 14pt;")
        self.patient_data_layout.addWidget(self.dni_label)

        # Edad
        self.age_label = QLabel(f"<b>Edad:</b> {self.patient_data['edad']}")
        self.age_label.setStyleSheet("color: white; font-size: 14pt;")
        self.patient_data_layout.addWidget(self.age_label)

        # Altura
        self.height_label = QLabel(f"<b>Altura:</b> {self.patient_data['altura']} cm")
        self.height_label.setStyleSheet("color: white; font-size: 14pt;")
        self.patient_data_layout.addWidget(self.height_label)

        # Sexo
        self.sex_label = QLabel(f"<b>Sexo:</b> {self.patient_data['sexo']}")
        self.sex_label.setStyleSheet("color: white; font-size: 14pt;")
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
        self.results_layout.setContentsMargins(8, 8, 8, 6)
        self.results_layout.setSpacing(0)
        self.results_frame.setFixedSize(500, 370)

        # --- Crear gráfico crPWV vs Edad ---
        self.pwv_graph_container = QWidget()
        self.pwv_graph_container.setStyleSheet("background-color: #000000;")
        self.pwv_graph_container_layout = QVBoxLayout()
        self.pwv_graph_container_layout.setContentsMargins(0, 0, 0, 2)
        self.pwv_graph_container_layout.setSpacing(0)
        self.pwv_graph_container.setLayout(self.pwv_graph_container_layout)

        self.pwv_graph = pg.PlotWidget()
        self.pwv_graph.setBackground('k')
        self.pwv_graph.showGrid(x=True, y=True)
        plot_item = self.pwv_graph.getPlotItem()
        axis_label_style = {'color': '#FFFFFF', 'font-size': '11pt'}
        left_axis = plot_item.getAxis('left')
        bottom_axis = plot_item.getAxis('bottom')
        left_axis.setLabel('crPWV (m/s)', **axis_label_style)
        bottom_axis.setLabel('')
        bottom_axis.label.setRotation(0)
        left_axis.showLabel(True)
        bottom_axis.showLabel(False)
        left_axis.setPen(pg.mkPen('w'))
        left_axis.setTextPen(pg.mkPen('w'))
        bottom_axis.setPen(pg.mkPen('w'))
        bottom_axis.setTextPen(pg.mkPen('w'))
        left_axis.setWidth(52)
        bottom_axis.setHeight(46)
        left_axis.setStyle(autoExpandTextSpace=True, tickTextOffset=8)
        bottom_axis.setStyle(autoExpandTextSpace=True, tickTextOffset=8)

        # --- Curvas de referencia crPWV (paper) ---
        x_fit = np.linspace(10, 100, 550)
        y_center = 7.37 * np.exp(0.0022 * x_fit)               # curva central
        y_upper = 12.0 * np.exp(0.0022 * x_fit)                # curva superior (corta y=12 en x=0)
        y_lower = 4.0 * np.exp(0.0022 * x_fit)                 # curva inferior (corta y=4 en x=0)

        # Ejes fijos solicitados
        self.pwv_graph.setXRange(10, 100, padding=0)
        self.pwv_graph.setYRange(1, 18, padding=0)
        self.pwv_graph.setLimits(xMin=10, xMax=100, yMin=1, yMax=18)

        # Líneas auxiliares para sombreado de zonas
        y_top = np.full_like(x_fit, 18.0)
        y_bottom = np.full_like(x_fit, 1.0)
        top_item = pg.PlotDataItem(x_fit, y_top, pen=None)
        bottom_item = pg.PlotDataItem(x_fit, y_bottom, pen=None)

        # Curvas punteadas (central verde, límites rojos)
        dashed_green = pg.mkPen((0, 220, 0), width=2, style=Qt.PenStyle.DashLine)
        dashed_red = pg.mkPen((255, 80, 80), width=1.6, style=Qt.PenStyle.DashLine)
        center_item = pg.PlotDataItem(x_fit, y_center, pen=dashed_green)
        upper_item = pg.PlotDataItem(x_fit, y_upper, pen=dashed_red)
        lower_item = pg.PlotDataItem(x_fit, y_lower, pen=dashed_red)

        # Sombreado: interior verde, exterior rojo (dentro del rango visible del eje)
        fill_inside = pg.FillBetweenItem(upper_item, lower_item, brush=pg.mkBrush(0, 255, 0, 70))
        fill_upper_out = pg.FillBetweenItem(top_item, upper_item, brush=pg.mkBrush(255, 0, 0, 55))
        fill_lower_out = pg.FillBetweenItem(lower_item, bottom_item, brush=pg.mkBrush(255, 0, 0, 55))

        self.pwv_graph.addItem(top_item)
        self.pwv_graph.addItem(bottom_item)
        self.pwv_graph.addItem(fill_upper_out)
        self.pwv_graph.addItem(fill_lower_out)
        self.pwv_graph.addItem(fill_inside)
        self.pwv_graph.addItem(upper_item)
        self.pwv_graph.addItem(lower_item)
        self.pwv_graph.addItem(center_item)

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
        self.pwv_graph_container_layout.addWidget(self.pwv_graph)
        self.pwv_graph_xlabel = QLabel("Edad (a\u00f1os)")
        self.pwv_graph_xlabel.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.pwv_graph_xlabel.setStyleSheet("color: white; font-size: 11pt; padding-left: 16px;")
        self.pwv_graph_container_layout.addWidget(self.pwv_graph_xlabel)
        self.results_layout.addWidget(self.pwv_graph_container)



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
        self.graph1.setLabel('left', 'Amplitud Normalizada (%)')
        self.graph1.setLabel('bottom', 'Tiempo (s)')
        self.graph1.enableAutoRange(axis='y', enable=False)
        self.graph1.setYRange(-100.0, 100.0, padding=0)
        axis1 = self.graph1.getAxis('left')
        axis1.setTextPen(pg.mkPen('#d0d0d0'))
        axis1.setPen(pg.mkPen('#6f6f6f'))
        axis1.setTicks([[
            (-100.0, "-100"),
            (-50.0, "-50"),
            (0.0, "0"),
            (50.0, "50"),
            (100.0, "100"),
        ]])
        self.right_layout.addWidget(self.graph1)

        # Gráfico sensor 2 (distal)
        self.graph2 = pg.PlotWidget()
        self.graph2.setBackground('k')
        self.graph2.setTitle("Sensor Distal (Radial)", color='w', size='12pt')
        self.graph2.showGrid(x=True, y=True)
        self.graph2.setLabel('left', 'Amplitud Normalizada (%)')
        self.graph2.setLabel('bottom', 'Tiempo (s)')
        self.graph2.enableAutoRange(axis='y', enable=False)
        self.graph2.setYRange(-100.0, 100.0, padding=0)
        axis2 = self.graph2.getAxis('left')
        axis2.setTextPen(pg.mkPen('#d0d0d0'))
        axis2.setPen(pg.mkPen('#6f6f6f'))
        axis2.setTicks([[
            (-100.0, "-100"),
            (-50.0, "-50"),
            (0.0, "0"),
            (50.0, "50"),
            (100.0, "100"),
        ]])
        self.right_layout.addWidget(self.graph2)

        # Curvas de datos
        self.curve1 = self.graph1.plot([], [], pen=pg.mkPen('red', width=2))
        self.curve2 = self.graph2.plot([], [], pen=pg.mkPen('pink', width=2))
        self.curve1.setClipToView(True)
        self.curve2.setClipToView(True)
        self.curve1.setDownsampling(auto=True, method='subsample')
        self.curve2.setDownsampling(auto=True, method='subsample')

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

        # crPWV
        self.pwv_label = QLabel("crPWV: -- m/s")
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

    def _enviar_reset_estudio_remoto(self):
        try:
            ComunicacionMax.enviar_reset_estudio()
        except Exception:
            pass

    def _try_send_patient_data(self):
        try:
            h_val = int(self.patient_data.get('altura', 170))
            a_val = int(self.patient_data.get('edad', 30))
            ok = ComunicacionMax.enviar_datos_paciente(h_val, a_val)
            if ok:
                self._patient_data_sent = True
            return ok
        except Exception:
            return False

    def _limpiar_sesion_local(self):
        self.measuring = False
        self.stop_graph_update()
        processor.stop_session()
        processor.clear_buffers()
        ComunicacionMax.reset_stream_buffers()
        self._session_started_once = False
        self._show_calibrating_until_ready = False
        self._patient_data_sent = False
        self._last_patient_data_send_attempt = 0.0
    
    # Volver a la ventana de inicio
    def go_back(self):
        # Popup de advertencia
        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Icon.Warning)
        msg.setWindowTitle("Confirmar Salida")
        msg.setText("¿Desea salir de la medición actual?")
        msg.setInformativeText("Se perderán los datos que no hayan sido guardados.")
        font = QFont()
        font.setPointSize(14)
        msg.setFont(font)

        # Botones personalizados para el popup
        si_button = msg.addButton("Salir", QMessageBox.ButtonRole.YesRole)
        no_button = msg.addButton("Cancelar", QMessageBox.ButtonRole.NoRole)

        si_button.setStyleSheet("""
            QPushButton {
                background-color: #F44336; color: white; font-size: 12pt;
                padding: 10px 20px; border-radius: 5px;
            }
            QPushButton:hover { background-color: #D32F2F; }
        """)
        no_button.setStyleSheet("""
            QPushButton {
                background-color: #424242; color: white; font-size: 12pt;
                padding: 10px 20px; border-radius: 5px;
            }
            QPushButton:hover { background-color: #616161; }
        """)

        msg.exec()

        # Si el usuario confirma, volvemos al inicio
        if msg.clickedButton() == si_button:
            self._enviar_reset_estudio_remoto()
            self._limpiar_sesion_local()

            self.welcome_screen = WelcomeScreen()
            self.welcome_screen.resize(self.size())
            self.welcome_screen.move(self.pos())
            if self.isMaximized():
                self.welcome_screen.showMaximized()
            else:
                self.welcome_screen.show()
            QTimer.singleShot(50, self.close)

    '''
    Esta funcion no tiene el popup

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
    '''


    # Realizar una medición con nuevo paciente
    def back_to_patient_data(self):
        # Popup de advertencia
        msg = QMessageBox(self)
        msg.setIcon(QMessageBox.Icon.Warning)
        msg.setWindowTitle("Nuevo Paciente")
        msg.setText("¿Desea iniciar una medición con un nuevo paciente?")
        msg.setInformativeText("Se perderán los datos de la medición actual.")
        
        font = QFont()
        font.setPointSize(14)
        msg.setFont(font)


        # Botones personalizados para el popup
        si_button = msg.addButton("Salir", QMessageBox.ButtonRole.YesRole)
        no_button = msg.addButton("Cancelar", QMessageBox.ButtonRole.NoRole)

        si_button.setStyleSheet("""
            QPushButton {
                background-color: #F44336; color: white; font-size: 12pt;
                padding: 10px 20px; border-radius: 5px; 
            }
            QPushButton:hover { background-color: #D32F2F; }
        """)
        no_button.setStyleSheet("""
            QPushButton {
                background-color: #424242; color: white; font-size: 12pt;
                padding: 10px 20px; border-radius: 5px;
            }
            QPushButton:hover { background-color: #616161; }
        """)

        msg.exec()

        if msg.clickedButton() == si_button:
            self._enviar_reset_estudio_remoto()
            self._limpiar_sesion_local()

           
            self.patient_data_window = PatientDataScreen()
            self.patient_data_window.resize(self.size())
            self.patient_data_window.move(self.pos())
            if self.isMaximized():
                self.patient_data_window.showMaximized()
            else:
                self.patient_data_window.show()
            QTimer.singleShot(50, self.close)


    '''
    Esta funcion no tiene el popup


    def back_to_patient_data(self):

        # Limpiamos el buffer y el valor de crPWV del backend para que el nuevo paciente comience de cero.
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
    '''
    

    # Iniciar medición
    def toggle_measurement(self):
        if not self.measuring:
            now = time.monotonic()
            self._last_patient_data_send_attempt = now
            self._try_send_patient_data()

            first_start = (not self._session_started_once)
            self.measuring = True
            now = time.monotonic()

            if first_start:
                ComunicacionMax.reset_stream_buffers()
                processor.start_session()
                self._session_started_once = True
                self._show_calibrating_until_ready = True
                self._patient_data_sent = False
                self._last_patient_data_send_attempt = now
                self._try_send_patient_data()

                self._last_curve1_data_time = now
                self._last_curve2_data_time = now
                self._last_x_end = None
                self._last_x_range = None
                self._last_valid_hr = None
                self._last_valid_pwv = None
                self._last_plot_seq = -1
                self._last_allow_signal_plot = None

                self.graph1.setYRange(-100.0, 100.0, padding=0)
                self.graph2.setYRange(-100.0, 100.0, padding=0)
                self.graph1.getAxis('left').setTextPen(pg.mkPen('#d0d0d0'))
                self.graph1.getAxis('left').setPen(pg.mkPen('#6f6f6f'))
                self.graph2.getAxis('left').setTextPen(pg.mkPen('#d0d0d0'))
                self.graph2.getAxis('left').setPen(pg.mkPen('#6f6f6f'))
                self.graph1.getAxis('left').setTicks([[
                    (-100.0, "-100"),
                    (-50.0, "-50"),
                    (0.0, "0"),
                    (50.0, "50"),
                    (100.0, "100"),
                ]])
                self.graph2.getAxis('left').setTicks([[
                    (-100.0, "-100"),
                    (-50.0, "-50"),
                    (0.0, "0"),
                    (50.0, "50"),
                    (100.0, "100"),
                ]])
                self.curve1.setData([], [])
                self.curve2.setData([], [])
                self.prox_alert_label.setVisible(False)
                self.dist_alert_label.setVisible(False)
                self.hr_esp_label.setText("HR: -- bpm")
                self.pwv_label.setText("crPWV: -- m/s")
                self.patient_point_item.setData([], [])
            else:
                self._last_curve1_data_time = now
                self._last_curve2_data_time = now
                self._last_allow_signal_plot = None
                metrics = processor.get_metrics()
                self._show_calibrating_until_ready = bool(metrics.get("calibrating", False))

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

    # Arranca el grafico
    def start_graph_update(self):
        self.timer = QTimer()
        self.timer.setTimerType(Qt.TimerType.PreciseTimer)
        self.timer.timeout.connect(self.update_plot)
        self.timer.start(20)  # ~50 Hz para visualizacion mas fluida

    # Detiene el gráfico
    def stop_graph_update(self):
        if hasattr(self, 'timer'):
            self.timer.stop()

    def _show_calibrating_alert(self, label, bg_color):
        label.setText("CALIBRANDO")
        label.setStyleSheet(f"""
            background-color: {bg_color};
            color: white;
            font-size: 22pt;
            font-weight: bold;
            border-radius: 10px;
        """)
        label.setVisible(True)

    def _show_connection_alert(self, label, bg_color):
        label.setText("SIN CONEXION ESP32")
        label.setStyleSheet(f"""
            background-color: {bg_color};
            color: white;
            font-size: 20pt;
            font-weight: bold;
            border-radius: 10px;
        """)
        label.setVisible(True)

    def _show_sensor_alert(self, label, sensor_name, connected_ok, skin_ok, bg_color):
        if not connected_ok:
            label.setText(f"{sensor_name} DESCONECTADO")
            label.setStyleSheet(f"""
                background-color: {bg_color};
                color: white;
                font-size: 18pt;
                font-weight: bold;
                border-radius: 10px;
            """)
            label.setVisible(True)
        elif not skin_ok:
            label.setText("REVISAR SENSOR")
            label.setStyleSheet(f"""
                background-color: {bg_color};
                color: white;
                font-size: 22pt;
                font-weight: bold;
                border-radius: 10px;
            """)
            label.setVisible(True)
        else:
            label.setVisible(False)

    def _to_float_or_none(self, value):
        if value is None:
            return None
        if isinstance(value, str):
            value = value.strip().replace(",", ".")
        try:
            v = float(value)
        except (TypeError, ValueError):
            return None
        if not math.isfinite(v):
            return None
        return v

    def _to_int_or_none(self, value):
        v = self._to_float_or_none(value)
        if v is None:
            return None
        iv = int(round(v))
        return iv if iv > 0 else None

    # Actualiza el grafico
    def update_plot(self):
        if not self.measuring:
            return

        processor.process_all()
        t, y1, y2 = processor.get_signals()
        status = processor.get_sensor_status()
        metrics = processor.get_metrics()

        c1 = status.get("c1", False)
        c2 = status.get("c2", False)
        s1 = status.get("s1", False)
        s2 = status.get("s2", False)
        ws_connected = bool(status.get("connected", False))
        now = time.monotonic()
        if (not self._patient_data_sent) and (now - self._last_patient_data_send_attempt >= self._patient_data_retry_sec):
            self._last_patient_data_send_attempt = now
            self._try_send_patient_data()

        calibrating = bool(metrics.get("calibrating", False))
        buffer_ready = bool(metrics.get("buffer_ready", not calibrating))
        both_signals_ok = c1 and c2 and s1 and s2

        if not ws_connected:
            self._show_connection_alert(self.prox_alert_label, "#b00020")
            self._show_connection_alert(self.dist_alert_label, "#c2185b")
        else:
            if self._show_calibrating_until_ready and buffer_ready and both_signals_ok:
                self._show_calibrating_until_ready = False

            if self._show_calibrating_until_ready and both_signals_ok:
                self._show_calibrating_alert(self.prox_alert_label, "#b00020")
                self._show_calibrating_alert(self.dist_alert_label, "#c2185b")
            else:
                self._show_sensor_alert(self.prox_alert_label, "SENSOR PROXIMAL", c1, s1, "#b00020")
                self._show_sensor_alert(self.dist_alert_label, "SENSOR DISTAL", c2, s2, "#c2185b")

        if len(t) > 1:
            x_end = t[-1]
            self._last_x_end = x_end
            x_start = max(0.0, x_end - 6.0)
            new_x_range = (round(x_start, 3), round(x_end, 3))
            if self._last_x_range != new_x_range:
                self.graph1.setXRange(x_start, x_end, padding=0)
                self.graph2.setXRange(x_start, x_end, padding=0)
                self._last_x_range = new_x_range
        elif self._last_x_end is not None:
            x_start = max(0.0, self._last_x_end - 6.0)
            new_x_range = (round(x_start, 3), round(self._last_x_end, 3))
            if self._last_x_range != new_x_range:
                self.graph1.setXRange(x_start, self._last_x_end, padding=0)
                self.graph2.setXRange(x_start, self._last_x_end, padding=0)
                self._last_x_range = new_x_range
        else:
            if self._last_x_range != (0.0, 6.0):
                self.graph1.setXRange(0.0, 6.0, padding=0)
                self.graph2.setXRange(0.0, 6.0, padding=0)
                self._last_x_range = (0.0, 6.0)

        allow_signal_plot = buffer_ready and (not self._show_calibrating_until_ready)
        data_seq = int(metrics.get("data_seq", -1))
        plot_mode_changed = (self._last_allow_signal_plot is None) or (self._last_allow_signal_plot != allow_signal_plot)

        if not allow_signal_plot:
            if plot_mode_changed:
                self.curve1.setData([], [])
                self.curve2.setData([], [])
        else:
            should_plot_update = plot_mode_changed or (data_seq != self._last_plot_seq)
            if should_plot_update:
                if len(t) == len(y1) and len(y1) > 1:
                    self.curve1.setData(t, y1)
                    self._last_curve1_data_time = now
                elif len(y1) > 0:
                    if (now - self._last_curve1_data_time) > self._curve_hold_seconds:
                        self.curve1.setData([], [])
                else:
                    self.curve1.setData([], [])

                if len(t) == len(y2) and len(y2) > 1:
                    self.curve2.setData(t, y2)
                    self._last_curve2_data_time = now
                elif len(y2) > 0:
                    if (now - self._last_curve2_data_time) > self._curve_hold_seconds:
                        self.curve2.setData([], [])
                else:
                    self.curve2.setData([], [])
                self._last_plot_seq = data_seq

        self._last_allow_signal_plot = allow_signal_plot

        hr_val = self._to_int_or_none(metrics.get("hr"))
        if hr_val is not None:
            self._last_valid_hr = hr_val
        if self._last_valid_hr is not None:
            self.hr_esp_label.setText(f"HR: {self._last_valid_hr} bpm")
        elif c1 and c2 and s1 and s2:
            self.hr_esp_label.setText("HR: Calculando...")
        else:
            self.hr_esp_label.setText("HR: -- bpm")

        pwv_val = self._to_float_or_none(metrics.get("pwv"))
        if pwv_val is not None and pwv_val > 0.0:
            self._last_valid_pwv = pwv_val
        if self._last_valid_pwv is not None:
            self.pwv_label.setText(f"crPWV: {self._last_valid_pwv:.1f} m/s")
            if self.patient_age is not None:
                self.patient_point_item.setData([self.patient_age], [self._last_valid_pwv])
        elif c1 and c2 and s1 and s2:
            self.pwv_label.setText("crPWV: Calculando...")
        else:
            self.pwv_label.setText("crPWV: -- m/s")


    # Guardar medicion
    def save_measurement(self):
        filename = resource_path("mediciones_pwv.csv")

        # Datos del paciente
        # (Usamos .get() para evitar errores si una clave no existe)
        nombre = self.patient_data.get('nombre', 'N/A')
        apellido = self.patient_data.get('apellido', 'N/A')
        dni = self.patient_data.get('dni', 'N/A')
        edad = self.patient_data.get('edad', 'N/A')
        altura = self.patient_data.get('altura', 'N/A')
        sexo = self.patient_data.get('sexo', 'N/A')
        observaciones = self.patient_data.get('observaciones', '')

        # Obtener métricas del backend (los valores ya promediados)
        metrics = processor.get_metrics()
        pwv_val = self._to_float_or_none(metrics.get('pwv'))
        hr_val = self._to_int_or_none(metrics.get('hr'))
        if pwv_val is None:
            pwv_val = self._last_valid_pwv
        if hr_val is None:
            hr_val = self._last_valid_hr

        # Validar que tengamos datos
        if pwv_val is None or hr_val is None:
            QMessageBox.warning(self, "Datos Incompletos",
                                "No se puede guardar la medición.\n"
                                "Asegúrese de que la crPWV y la HR se estén midiendo.")
            return

        # Formatear los datos
        pwv_str = f"{pwv_val:.1f}"
        hr_str = f"{hr_val:.0f}"
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        # Encabezado
        header = ["Fecha y Hora", "DNI", "Nombre", "Apellido", "Edad", "Altura (cm)", "Sexo", "HR (bpm)", "crPWV (m/s)", "Observaciones"]
        data_row = [timestamp, dni,  nombre, apellido, edad, altura, sexo, hr_str, pwv_str, observaciones]

        # Escribir en el archivo CSV
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

