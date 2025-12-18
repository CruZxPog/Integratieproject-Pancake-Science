import os
import mysql.connector

def create_database():
    db = mysql.connector.connect(
        host=os.getenv("DB_HOST", "127.0.0.1"),
        port=int(os.getenv("DB_PORT", "3306")),
        user=os.getenv("DB_USER"),
        password=os.getenv("DB_PASSWORD"),
        database=os.getenv("DB_NAME"),
    )
    cur = db.cursor()

    sql = """
    CREATE TABLE IF NOT EXISTS users (
        id INT AUTO_INCREMENT PRIMARY KEY,
        username VARCHAR(100) NOT NULL UNIQUE,
        password_hash VARCHAR(255) NOT NULL
    );
    """
    cur.execute(sql)

    sql = """
    CREATE TABLE IF NOT EXISTS programs (
        id INT AUTO_INCREMENT PRIMARY KEY,
        user_id INT NOT NULL,
        name VARCHAR(100) NOT NULL,
        FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
    );
    """
    cur.execute(sql)

    sql = """
    CREATE TABLE IF NOT EXISTS sessions (
        id INT AUTO_INCREMENT PRIMARY KEY,
        program_id INT NOT NULL,
        start_time TIMESTAMP NOT NULL,
        end_time TIMESTAMP NULL,
        FOREIGN KEY (program_id) REFERENCES programs(id) ON DELETE CASCADE
    );
    """
    cur.execute(sql)

    sql = """
    CREATE TABLE IF NOT EXISTS program_settings (
        id INT AUTO_INCREMENT PRIMARY KEY,
        program_id INT NOT NULL,
        phase VARCHAR(50) NOT NULL,
        target_temperature FLOAT NOT NULL,
        FOREIGN KEY (program_id) REFERENCES programs(id) ON DELETE CASCADE
    );
    """
    cur.execute(sql)

    sql = """
    CREATE TABLE IF NOT EXISTS measurements (
        id INT AUTO_INCREMENT PRIMARY KEY,
        session_id INT NOT NULL,
        temperature FLOAT NOT NULL,
        phase VARCHAR(50) NOT NULL,
        timestamp TIMESTAMP NOT NULL,
        FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
    );
    """
    cur.execute(sql)

    sql = "CREATE INDEX idx_programs_user_id ON programs(user_id);"
    cur.execute(sql)

    sql = "CREATE INDEX idx_sessions_program_id ON sessions(program_id);"
    cur.execute(sql)

    sql = "CREATE INDEX idx_measurements_session_id ON measurements(session_id);"
    cur.execute(sql)

    sql = "CREATE INDEX idx_measurements_timestamp ON measurements(timestamp);"
    cur.execute(sql)

    db.commit()
    cur.close()
    db.close()

if __name__ == "__main__":
    create_database()