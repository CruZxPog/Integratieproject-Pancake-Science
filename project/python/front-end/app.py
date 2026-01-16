from flask import Flask, request, jsonify, render_template
from db import add_user, authenticate_user, get_user_by_username, \
    create_program_with_settings, get_programs_for_user, \
    program_belongs_to_user, create_session, get_sessions_for_program, get_measurements_for_session
from mqtt import publish_program_to_arduino, publish_wifi_settings

app = Flask(__name__)
app.secret_key = "supersecretkey"

# ----------------------------
# In-memory WiFi settings voor Raspberry Pi
# ----------------------------
WIFI_SETTINGS = {}

# ----------------------------
# HTML routes
# ----------------------------
@app.route("/")
def index():
    return render_template("index.html")

@app.route("/create.html")
def create():
    return render_template("create.html")

@app.route("/wifi.html")
def wifi():
    return render_template("wifi.html")

@app.route("/dashboard.html")
def dashboard():
    return render_template("dashboard.html")

@app.route("/start.html")
def start():
    return render_template("start.html")

@app.route("/programma.html")
def programma():
    return render_template("programma.html")

@app.route("/session.html")
def session():
    return render_template("session.html")

@app.route("/session_detail.html")
def session_detail():
    return render_template("session_detail.html")

# ----------------------------
# User APIs
# ----------------------------
@app.route("/api/register", methods=["POST"])
def api_register():
    data = request.json
    username = data.get("username")
    password = data.get("password")
    if not username or not password:
        return jsonify({"status":"error","message":"Vul gebruikersnaam en wachtwoord in"}),400
    res = add_user(username,password)
    if isinstance(res,int):
        return jsonify({"status":"ok","user_id":res})
    return jsonify({"status":"error","message":res}),400

@app.route("/api/login", methods=["POST"])
def api_login():
    data = request.json
    username = data.get("username")
    password = data.get("password")
    res = authenticate_user(username,password)
    if isinstance(res,dict):
        return jsonify({"status":"ok","user":res})
    return jsonify({"status":"error","message":res}),401

# ----------------------------
# Program APIs
# ----------------------------
@app.route("/api/programs", methods=["GET","POST"])
def api_programs():
    user_id = int(request.args.get("user_id",0))
    if request.method == "GET":
        programs = get_programs_for_user(user_id)
        return jsonify(programs)
    if request.method == "POST":
        data = request.json
        name = data.get("name")
        phases = data.get("phases",[])
        res = create_program_with_settings(user_id,name,phases)
        if isinstance(res,int):
            return jsonify({"status":"ok","program_id":res})
        return jsonify({"status":"error","message":res}),400

@app.route("/api/programs/<int:program_id>", methods=["GET","PUT"])
def api_single_program(program_id):
    user_id = int(request.args.get("user_id",0))
    if request.method == "GET":
        programs = get_programs_for_user(user_id)
        program = next((p for p in programs if p["id"]==program_id),None)
        if program:
            return jsonify(program)
        return jsonify({"status":"error","message":"Programma niet gevonden"}),404
    if request.method == "PUT":
        data = request.json
        name = data.get("name")
        phases = data.get("phases",[])
        res = create_program_with_settings(user_id,name,phases)
        if isinstance(res,int):
            return jsonify({"status":"ok","program_id":res})
        return jsonify({"status":"error","message":res}),400

# ----------------------------
# Sessions APIs (zonder naam)
# ----------------------------
@app.route("/api/programs/<int:program_id>/sessions", methods=["GET","POST"])
def api_sessions(program_id):
    user_id = int(request.args.get("user_id", 0))

    if request.method == "GET":
        sessions = get_sessions_for_program(user_id, program_id)
        return jsonify(sessions)

    if request.method == "POST":
        res = create_session(user_id, program_id)

        if not isinstance(res, int):
            return jsonify({
                "status": "error",
                "message": res
            }), 400

        session_id = res

        pub_res = publish_program_to_arduino(user_id, program_id, session_id)
        if pub_res is not True:
            return jsonify({
                "status": "error",
                "message": f"sessie gemaakt (id={session_id}) maar MQTT sturen faalde: {pub_res}",
                "session_id": session_id
            }), 500

        return jsonify({
            "status": "ok",
            "session_id": session_id
        })

@app.route("/api/sessions/<int:session_id>/measurements", methods=["GET","POST"])
def api_measurements(session_id):
    user_id = int(request.args.get("user_id",0))
    if request.method == "GET":
        measurements = get_measurements_for_session(user_id,session_id)
        return jsonify(measurements)
    if request.method == "POST":
        data = request.json
        # TODO: voeg data toe aan database via db.py
        return jsonify({"status":"ok","data":data})

# ----------------------------
# WiFi API voor Raspberry Pi
# ----------------------------
@app.route("/api/wifi", methods=["POST"])
def api_wifi():
    data = request.json
    ssid = data.get("ssid")
    password = data.get("password")
    pi_id = data.get("pi_id","default")
    if not ssid or not password:
        return jsonify({"status":"error","message":"SSID of wachtwoord ontbreekt"}),400
    publish_wifi_settings(ssid, password)
    return jsonify({"status":"ok"})

# ----------------------------
# Start app
# ----------------------------
if __name__=="__main__":
    app.run(debug=True)
