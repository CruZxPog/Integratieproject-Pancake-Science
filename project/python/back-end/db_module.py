import os
from datetime import datetime
from flask import g
import mysql.connector
import bcrypt
import dotenv

dotenv.load_dotenv()
def get_db():
    if 'db' not in g:
        g.db = mysql.connector.connect(
            host=os.getenv('DB_HOST', 'localhost'),
            user=os.getenv('DB_USER', 'user'),
            password=os.getenv('DB_PASSWORD', 'password'),
            database=os.getenv('DB_NAME', 'database')
        )
    return g.db

def add_user(username: str, password: str):
    password_hash = bcrypt.hashpw(
        password.encode("utf-8"),
        bcrypt.gensalt()
    ).decode("utf-8")  # store as TEXT/VARCHAR

    db = get_db()
    cursor = db.cursor()

    sql = "INSERT INTO users (username, password_hash) VALUES (%s, %s)"
    val = (username, password_hash)
    cursor.execute(sql, val)

    db.commit()
    cursor.close()

def get_user_by_username(username: str):
    db = get_db()
    cursor = db.cursor(dictionary=True)

    sql = "SELECT id, username, password_hash FROM users WHERE username = %s"
    val = (username,)
    cursor.execute(sql, val)
    user = cursor.fetchone()

    cursor.close()
    return user

def authenticate_user(username: str, password: str):
    user = get_user_by_username(username)
    if not user:
        return "user_not_found"

    stored_hash = user["password_hash"].encode("utf-8")  # back to bytes
    if bcrypt.checkpw(password.encode("utf-8"), stored_hash):
        return {"id": user["id"], "username": user["username"]}

    return "wrong_password"

def get_programs_for_user(user_id: int):
    db = get_db()
    cursor = db.cursor(dictionary=True)

    sql = """
        SELECT id, user_id, name
        FROM programs
        WHERE user_id = %s
        ORDER BY id DESC
    """
    val = (user_id,)
    cursor.execute(sql, val)
    programs = cursor.fetchall()

    cursor.close()
    return programs

def get_sessions_for_program(user_id: int, program_id: int):
    de = get_db()
    cursor = de.cursor(dictionary=True)

    sql = """
        SELECT s.id, s.program_id, s.start_time, s.end_time
        FROM sessions s
        JOIN programs p ON p.id = s.program_id
        WHERE s.program_id = %s AND p.user_id = %s
        ORDER BY s.start_time DESC
    """
    val = (program_id, user_id)
    cursor.execute(sql, val)
    sessions = cursor.fetchall()

    cursor.close()
    return sessions

def get_measurements_for_session(user_id: int, session_id: int):
    db = get_db()
    cursor = db.cursor(dictionary=True)

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
    rows = cursor.fetchall()

    cursor.close()
    return rows

def create_program(user_id: int, name: str):
    db = get_db()
    cursor = db.cursor()

    sql = """
        INSERT INTO programs (user_id, name)
        VALUES (%s, %s)
    """
    cursor.execute(sql, (user_id, name))
    db.commit()

    program_id = cursor.lastrowid
    cursor.close()
    return program_id

def create_session(user_id: int, program_id: int):
    db = get_db()
    cursor = db.cursor()

    # ownership check at DB level
    sql_check = """
        SELECT 1
        FROM programs
        WHERE id = %s AND user_id = %s
        LIMIT 1
    """
    cursor.execute(sql_check, (program_id, user_id))
    if cursor.fetchone() is None:
        cursor.close()
        return None  # program does not belong to user

    sql = """
        INSERT INTO sessions (program_id, start_time)
        VALUES (%s, NOW())
    """
    cursor.execute(sql, (program_id,))
    db.commit()

    session_id = cursor.lastrowid
    cursor.close()
    return session_id

def end_session(user_id: int, session_id: int):
    db = get_db()
    cursor = db.cursor()

    sql = """
        UPDATE sessions s
        JOIN programs p ON p.id = s.program_id
        SET s.end_time = NOW()
        WHERE s.id = %s AND p.user_id = %s
    """
    val = (session_id, user_id)
    cursor.execute(sql, val)

    db.commit()
    ok = cursor.rowcount > 0
    cursor.close()
    return ok

def loaddb_module():
    print("Database module loaded.")
if __name__ == "__main__":
    loaddb_module()