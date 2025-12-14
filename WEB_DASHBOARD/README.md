# 🔐 Face Recognition Attendance System - Web Dashboard

A real-time web dashboard for monitoring and managing face recognition attendance data from a Raspberry Pi 4 system.

## 🎯 Overview

This web dashboard is part of a comprehensive Face Recognition Attendance and Monitoring System running on Raspberry Pi 4. The system captures real-time video from a USB camera, performs facial detection and recognition using the MobileFaceNet AI model, and displays results on a 2.4" TFT LCD screen.

The web interface provides:
- Real-time monitoring of attendance records
- User management with CRUD operations
- Automatic synchronization with Firebase Realtime Database
- Responsive design for desktop and mobile devices

## ✨ Features

### 📊 Attendance Monitoring
- **Real-time Statistics**: View today's check-ins and total attendance records
- **Detailed Records**: Display all attendance with name, date, time, and ID
- **Search & Filter**: Quick search by name, date, or time
- **Auto-refresh**: Real-time updates from Firebase database
- **Sorted Display**: Most recent records shown first

### 👥 User Management
- **View All Users**: List of all registered users with their IDs
- **Edit User Names**: Update user names with automatic sync to attendance records
- **Delete Users**: Remove users from the system (attendance history preserved)
- **Search Users**: Filter users by name or ID
- **Real-time Updates**: Changes reflect immediately across all records

### 🎨 User Interface
- Modern gradient design with purple theme
- Fully responsive (mobile, tablet, desktop)
- Smooth animations and transitions
- Modal dialogs for editing
- Empty state indicators
- Loading states

## 🛠 Technologies

### Hardware
- **Raspberry Pi 4** - Main processing unit
- **USB Camera** - Video capture for face detection
- **2.4" TFT LCD** - Local display interface
- **Physical Button** - Face registration trigger

### Software - Raspberry Pi Side
- **C/C++** - Core application logic
- **OpenCV** - Computer vision processing
- **MobileFaceNet** - AI model for face detection & recognition
- **Firebase SDK** - Database synchronization

### Software - Web Dashboard
- **HTML5/CSS3** - Frontend structure and styling
- **Vanilla JavaScript** - Application logic
- **Firebase Realtime Database** - Cloud data storage
- **Firebase SDK 9.22.0** - Real-time data synchronization

## 📦 Installation

### Prerequisites
- A modern web browser (Chrome, Firefox, Safari, Edge)
- Internet connection for Firebase access
- (Optional) A local web server for development

### Steps

1. **Clone the repository**
```bash
git clone https://github.com/yourusername/face-recognition-dashboard.git
cd face-recognition-dashboard
```

2. **Configure Firebase**
   
   Open `firebase-config.js` and update with your Firebase configuration:
   ```javascript
   const firebaseConfig = {
       databaseURL: "YOUR_FIREBASE_DATABASE_URL"
   };
   ```

3. **Run the application**

Direct file access**
   - Simply open `index.html` in your web browser  

## 🚀 Usage

### Monitoring Attendance

1. Click on the **"📊 Attendance Records"** tab
2. View real-time statistics at the top
3. Browse through the attendance table
4. Use the search box to filter specific records

### Managing Users

1. Click on the **"👥 Registered Users"** tab
2. View all registered users in the system
3. **To edit a user:**
   - Click the "✏️ Edit" button
   - Enter the new name in the modal
   - Click "Save Changes"
   - All attendance records will automatically update
4. **To delete a user:**
   - Click the "🗑️ Delete" button
   - Confirm the deletion
   - Note: Attendance history is preserved

### Search Functionality

- **Attendance Search**: Type in name, date, or time to filter records
- **User Search**: Type user name or ID to find specific users
- **Real-time filtering**: Results update as you type

## 📁 Project Structure

```
face-recognition-dashboard/
│
├── index.html              # Main HTML structure
├── styles.css              # All CSS styling
├── firebase-config.js      # Firebase configuration
├── app.js                  # Main application logic
└── README.md              # This file
```

### File Descriptions

- **`index.html`**: Contains the HTML structure, tabs, tables, and modal dialogs
- **`styles.css`**: All styling including responsive design, animations, and themes
- **`firebase-config.js`**: Firebase initialization and database reference
- **`app.js`**: Core functionality including:
  - Data loading from Firebase
  - Display and rendering functions
  - CRUD operations
  - Search and filter logic
  - Name synchronization between users and attendance

## 🔥 Firebase Configuration

### Database Structure

```json
{
  "attendance": {
    "id_2025_12_01_21_29_30": {
      "date": "2025-12-01",
      "name": "Phong",
      "time": "21:31:46"
    }
  },
  "users": {
    "id_2025_12_01_21_29_30": {
      "name": "Phong",
      "embedding": [...]
    }
  }
}
```

### Security Rules

**Important**: Update your Firebase Security Rules for production:

For development/testing only:
```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

## 📸 Screenshots

### Attendance Records Tab
![Attendance Tab](screenshots/attendance-tab.png)
*Real-time attendance monitoring with search and statistics*

### User Management Tab
![Users Tab](screenshots/users-tab.png)
*Manage registered users with edit and delete functions*

### Edit User Modal
![Edit Modal](screenshots/edit-modal.png)
*Modal dialog for updating user names*

## 🔒 Security Considerations

1. **Firebase Security Rules**: Always use authentication in production
2. **Database URL**: Can be public if rules are properly configured
3. **No API Keys**: This project uses Firebase with public database URL
4. **HTTPS**: Host on HTTPS in production for secure data transmission
5. **XSS Protection**: Input sanitization implemented in code

## 🐛 Troubleshooting

### Dashboard not loading data
- Check Firebase database URL in `firebase-config.js`
- Verify Firebase Security Rules allow read access
- Check browser console for error messages
- Ensure internet connection is active

### Real-time updates not working
- Verify Firebase Realtime Database is active
- Check browser console for Firebase errors
- Ensure WebSocket connections are not blocked

### Search not working
- Clear browser cache and reload
- Check JavaScript console for errors
- Verify search input field is properly focused
