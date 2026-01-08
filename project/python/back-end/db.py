import os
from datetime import datetime
from flask import g
import mysql.connector

def get_db():
    if 'db' not in g:
        g.db = mysql.connector.connect(
            host=os.getenv('DB_HOST', 'localhost'),
            user=os.getenv('DB_USER', 'user'),
            password=os.getenv('DB_PASSWORD', 'password'),
            database=os.getenv('DB_NAME', 'database')
        )
    return g.db

def close_db(e=None):
    db = g.pop('db', None)
    if db is not None:
        db.close()

def init_db():
    db = get_db()
    cursor = db.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS users (
            id INT AUTO_INCREMENT PRIMARY KEY,
            username VARCHAR(150) NOT NULL UNIQUE,
            email VARCHAR(150) NOT NULL UNIQUE,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    db.commit()
    cursor.close()

def add_user(username, email):
    db = get_db()
    cursor = db.cursor()
    cursor.execute('''
        INSERT INTO users (username, email) VALUES (%s, %s)
    ''', (username, email))
    db.commit()
    cursor.close()

def get_user_by_username(username):
    db = get_db()
    cursor = db.cursor(dictionary=True)
    cursor.execute('''
        SELECT * FROM users WHERE username = %s
    ''', (username,))
    user = cursor.fetchone()
    cursor.close()
    return user

