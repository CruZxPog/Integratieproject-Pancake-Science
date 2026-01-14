import os
from dotenv import load_dotenv
import bcrypt
import mysql.connector
from mysql.connector.errors import IntegrityError

load_dotenv()

# ----------------------------
# Connection
# ----------------------------

def get_db():
    return mysql.connector.connect(
        host=os.getenv("DB_HOST"),
        port=int(os.getenv("DB_PORT")),
        user=os.getenv("DB_USER", "iot_user"),
        password=os.getenv("DB_PASSWORD", "pannenkoek_app"),
        database=os.getenv("DB_NAME", "pancake_science_db"),
        autocommit=False,
    )

# ----------------------------
# Users
# ----------------------------

def add_user(username, password):
    """
    Creates a new user.
    Returns new user id, or and error message if username already exists.
    """
    password_hash = bcrypt.hashpw(
        password.encode("utf-8"),
        bcrypt.gensalt()
    ).decode("utf-8")

    db = get_db()
    cursor = db.cursor()

    try:
        sql = "INSERT INTO users (username, password_hash) VALUES (%s, %s)"
        val = (username, password_hash)
        cursor.execute(sql, val)
        db.commit()
        return cursor.lastrowid

    except IntegrityError:
        db.rollback()
        return "db rolledback - username exists"

    finally:
        cursor.close()
        db.close()

def get_user_by_username(username):
    db = get_db()
    cursor = db.cursor(dictionary=True)

    try:
        sql = "SELECT id, username, password_hash FROM users WHERE username = %s"
        cursor.execute(sql, (username,))
        return cursor.fetchone()

    finally:
        cursor.close()
        db.close()

def authenticate_user(username, password):
    """
    Returns {id, username} if valid, else returns an error message.
    """
    user = get_user_by_username(username)
    if not user:
        return "user not found"

    stored_hash = user["password_hash"].encode("utf-8")
    if bcrypt.checkpw(password.encode("utf-8"), stored_hash):
        return {"id": user["id"], "username": user["username"]}

    return "Invalid username or password"

# ----------------------------
# Programs
# ----------------------------

def create_program(user_id, name):
    db = get_db()
    cursor = db.cursor()

    try:
        sql = "INSERT INTO programs (user_id, name) VALUES (%s, %s)"
        cursor.execute(sql, (user_id, name))
        db.commit()
        return cursor.lastrowid

    finally:
        cursor.close()
        db.close()

def get_programs_for_user(user_id):
    db = get_db()
    cursor = db.cursor(dictionary=True)

    try:
        sql = """
            SELECT id, name
            FROM programs
            WHERE user_id = %s
            ORDER BY id DESC
        """
        cursor.execute(sql, (user_id,))
        return cursor.fetchall()

    finally:
        cursor.close()
        db.close()

def program_belongs_to_user(user_id, program_id):
    db = get_db()
    cursor = db.cursor()

    try:
        sql = "SELECT 1 FROM programs WHERE id = %s AND user_id = %s LIMIT 1"
        cursor.execute(sql, (program_id, user_id))
        return cursor.fetchone() is not None

    finally:
        cursor.close()
        db.close()

def create_program_with_settings(user_id, program_name, phases):
    """
    phases = [
        {"phase": "heatup", "target_temperature": 180.0},
        {"phase": "cook", "target_temperature": 195.0},
        ...
    ]

    Returns new program_id, or an error message on failure.
    """
    db = get_db()
    cursor = db.cursor()

    try:
        # 1) create program
        sql_program = "INSERT INTO programs (user_id, name) VALUES (%s, %s)"
        cursor.execute(sql_program, (user_id, program_name))
        program_id = cursor.lastrowid

        # 2) add phases
        sql_phase = """
            INSERT INTO program_settings (program_id, phase, target_temperature)
            VALUES (%s, %s, %s)
        """

        for item in phases:
            phase_name = (item.get("phase") or "").strip()
            target_temp = item.get("target_temperature", None)

            if not phase_name:
                raise ValueError("Phase name is required")

            if target_temp is None:
                raise ValueError("Target temperature is required")

            cursor.execute(sql_phase, (program_id, phase_name, float(target_temp)))

        db.commit()
        return program_id

    except Exception:
        db.rollback()
        return "db rolledback"

    finally:
        cursor.close()
        db.close()

# ----------------------------
# Sessions
# ----------------------------

def create_session(user_id, program_id):
    """
    Starts a new session (ownership enforced).
    Returns new session id or an error message.
    """
    if not program_belongs_to_user(user_id, program_id):
        return "program does not belong to this user"

    db = get_db()
    cursor = db.cursor()

    try:
        sql = "INSERT INTO sessions (program_id, start_time) VALUES (%s, NOW())"
        cursor.execute(sql, (program_id,))
        db.commit()
        return cursor.lastrowid

    finally:
        cursor.close()
        db.close()

def end_session(user_id, session_id):
    """
    Ends a session by setting end_time = NOW() (ownership enforced).
    """
    db = get_db()
    cursor = db.cursor()

    try:
        sql = """
            UPDATE sessions s
            JOIN programs p ON p.id = s.program_id
            SET s.end_time = NOW()
            WHERE s.id = %s AND p.user_id = %s
        """
        cursor.execute(sql, (session_id, user_id))
        db.commit()
        return cursor.rowcount > 0

    finally:
        cursor.close()
        db.close()

def get_sessions_for_program(user_id, program_id):
    """
    Returns all sessions for a program (ownership enforced).
    """
    db = get_db()
    cursor = db.cursor(dictionary=True)

    try:
        sql = """
            SELECT s.id, s.start_time, s.end_time
            FROM sessions s
            JOIN programs p ON p.id = s.program_id
            WHERE s.program_id = %s AND p.user_id = %s
            ORDER BY s.start_time DESC
        """
        cursor.execute(sql, (program_id, user_id))
        return cursor.fetchall()

    finally:
        cursor.close()
        db.close()

def get_session(user_id, session_id):
    """
    Returns session metadata if it belongs to the user.
    """
    db = get_db()
    cursor = db.cursor(dictionary=True)

    try:
        sql = """
            SELECT s.id, s.program_id, s.start_time, s.end_time
            FROM sessions s
            JOIN programs p ON p.id = s.program_id
            WHERE s.id = %s AND p.user_id = %s
            LIMIT 1
        """
        cursor.execute(sql, (session_id, user_id))
        return cursor.fetchone()

    finally:
        cursor.close()
        db.close()

# ----------------------------
# Measurements
# ----------------------------

def get_measurements_for_session(user_id, session_id):
    """
    Returns full measurement dataset for a session (ownership enforced).
    """
    db = get_db()
    cursor = db.cursor(dictionary=True)

    try:
        sql = """
            SELECT
                m.id,
                m.session_id,
                m.temperature,
                m.phase,
                m.timestamp
            FROM measurements m
            JOIN sessions s ON s.id = m.session_id
            JOIN programs p ON p.id = s.program_id
            WHERE m.session_id = %s AND p.user_id = %s
            ORDER BY m.timestamp ASC, m.id ASC
        """
        cursor.execute(sql, (session_id, user_id))
        return cursor.fetchall()

    finally:
        cursor.close()
        db.close()

# ----------------------------
# Program settings (optional helpers)
# ----------------------------

def get_program_settings(user_id, program_id):
    """
    Optional helper for the settings page.
    Returns an error message if program not owned by user.
    """
    if not program_belongs_to_user(user_id, program_id):
        return "program does not belong to this user"

    db = get_db()
    cursor = db.cursor(dictionary=True)

    try:
        sql = """
            SELECT phase, target_temperature
            FROM program_settings
            WHERE program_id = %s
            ORDER BY phase ASC
        """
        cursor.execute(sql, (program_id,))
        return cursor.fetchall()

    finally:
        cursor.close()
        db.close()

def load_dbmodule():
    if get_db() is None:
        return False
    
    print("DB module loaded.")
    return True

if __name__ == "__main__":
    load_dbmodule()