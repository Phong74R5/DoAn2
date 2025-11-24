from flask import Flask, render_template, request
import firebase_admin
from firebase_admin import credentials, db
from datetime import datetime
import os

# ----------------------------
# Initialize Firebase
# ----------------------------
cred_path = os.path.join(os.path.dirname(__file__), "serviceAccountKey.json")
cred = credentials.Certificate(cred_path)

# Kiểm tra nếu Firebase chưa được initialize
if not firebase_admin._apps:
    firebase_admin.initialize_app(cred, {
        'databaseURL': 'https://face-detect-60c0e-default-rtdb.firebaseio.com/'  # Thay bằng URL Realtime Database của bạn
    })

# Reference tới nhánh "attendance"
attendance_ref = db.reference('attendance')

# ----------------------------
# Initialize Flask
# ----------------------------
app = Flask(__name__)

# ----------------------------
# Routes
# ----------------------------
@app.route('/')
def index():
    # Khi load trang chính, gửi attendance_data rỗng
    return render_template('index.html', selected_date='', no_data=False, attendance_data=[])


@app.route('/attendance', methods=['POST'])
def attendance():
    selected_date = request.form.get('selected_date')

    try:
        selected_date_obj = datetime.strptime(selected_date, '%Y-%m-%d')
        formatted_date = selected_date_obj.strftime('%Y-%m-%d')
    except:
        # Nếu input không hợp lệ, trả về trang chính
        return render_template('index.html', selected_date='', no_data=True, attendance_data=[])

    # Lấy dữ liệu từ Firebase theo ngày
    all_data = attendance_ref.order_by_child('date').equal_to(formatted_date).get()

    attendance_data = []
    if all_data:
        for key, val in all_data.items():
            # val là dict với 'name' và 'time'
            attendance_data.append((val.get('name', ''), val.get('time', '')))

    no_data = len(attendance_data) == 0

    return render_template('index.html', selected_date=selected_date, no_data=no_data, attendance_data=attendance_data)


# ----------------------------
# Run Flask app
# ----------------------------
if __name__ == '__main__':
    app.run(debug=True)
