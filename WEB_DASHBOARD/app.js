// Global variables
let attendanceData = [];
let usersData = [];
let currentEditId = null;
// let currentEditOldName = null; // Không cần dùng biến này nữa

// Helper: Format timestamp unix -> Ngày giờ VN
function formatDateTime(unixTimestamp) {
    if (!unixTimestamp) return { date: '--', time: '--' };
    const dateObj = new Date(unixTimestamp * 1000);
    return {
        date: dateObj.toLocaleDateString('vi-VN'),
        time: dateObj.toLocaleTimeString('vi-VN')
    };
}

// Tab switching
function showTab(tabName) {
    document.querySelectorAll('.content').forEach(content => {
        content.classList.remove('active');
    });
    document.querySelectorAll('.tab').forEach(tab => {
        tab.classList.remove('active');
    });
    
    document.getElementById(tabName).classList.add('active');
    event.target.classList.add('active');
}

// ==================== ATTENDANCE FUNCTIONS ====================

// Load Attendance Data
function loadAttendance() {
    database.ref('attendance').on('value', (snapshot) => {
        attendanceData = [];
        const data = snapshot.val();
        
        if (data) {
            Object.keys(data).forEach(key => {
                attendanceData.push({
                    firebaseKey: key, // Lưu key để dùng nếu cần
                    id: data[key].id,
                    name: data[key].name,
                    timestamp_unix: data[key].timestamp_unix // Lấy timestamp từ C++
                });
            });
            
            // Sắp xếp: Mới nhất lên đầu (dựa vào timestamp_unix)
            attendanceData.sort((a, b) => (b.timestamp_unix || 0) - (a.timestamp_unix || 0));
        }
        
        displayAttendance();
        updateAttendanceStats();
    });
}

// Display Attendance
function displayAttendance() {
    const tbody = document.getElementById('attendanceBody');
    
    if (attendanceData.length === 0) {
        tbody.innerHTML = '<tr><td colspan="4" class="empty-state"><div class="empty-state-icon">📭</div><div>No attendance records found</div></td></tr>';
        return;
    }
    
    tbody.innerHTML = attendanceData.map(record => {
        // Xử lý hiển thị ngày giờ tại đây
        const dateTime = formatDateTime(record.timestamp_unix);
        
        return `
        <tr>
            <td><strong>${escapeHtml(record.name)}</strong></td>
            <td>${dateTime.date}</td>
            <td>${dateTime.time}</td>
            <td><small style="color: #999;">${record.id}</small></td>
        </tr>
    `}).join('');
}

// Update Attendance Stats
function updateAttendanceStats() {
    const now = new Date();
    const todayStr = now.toLocaleDateString('vi-VN');
    
    // Đếm check-in hôm nay
    const todayRecords = attendanceData.filter(r => {
        const dt = formatDateTime(r.timestamp_unix);
        return dt.date === todayStr;
    });
    
    document.getElementById('todayCount').textContent = todayRecords.length;
    document.getElementById('totalAttendance').textContent = attendanceData.length;
}

// Filter Attendance
function filterAttendance() {
    const searchTerm = document.getElementById('searchAttendance').value.toLowerCase();
    
    const filtered = attendanceData.filter(record => {
        const dt = formatDateTime(record.timestamp_unix);
        return (record.name && record.name.toLowerCase().includes(searchTerm)) ||
               (record.id && record.id.toLowerCase().includes(searchTerm)) ||
               (dt.date.includes(searchTerm));
    });
    
    const tbody = document.getElementById('attendanceBody');
    
    if (filtered.length === 0) {
        tbody.innerHTML = '<tr><td colspan="4" class="empty-state"><div>No matching records found</div></td></tr>';
        return;
    }
    
    tbody.innerHTML = filtered.map(record => {
        const dateTime = formatDateTime(record.timestamp_unix);
        return `
        <tr>
            <td><strong>${escapeHtml(record.name)}</strong></td>
            <td>${dateTime.date}</td>
            <td>${dateTime.time}</td>
            <td><small style="color: #999;">${record.id}</small></td>
        </tr>
    `}).join('');
}

// ==================== USERS FUNCTIONS ====================

// Load Users Data
function loadUsers() {
    database.ref('users').on('value', (snapshot) => {
        usersData = [];
        const data = snapshot.val();
        
        if (data) {
            Object.keys(data).forEach(key => {
                usersData.push({
                    id: key,
                    name: data[key].name
                });
            });
        }
        
        displayUsers();
        updateUsersStats();
    });
}

// Display Users
function displayUsers() {
    const tbody = document.getElementById('usersBody');
    
    if (usersData.length === 0) {
        tbody.innerHTML = '<tr><td colspan="3" class="empty-state"><div class="empty-state-icon">👥</div><div>No registered users found</div></td></tr>';
        return;
    }
    
    tbody.innerHTML = usersData.map(user => `
        <tr>
            <td><small style="color: #999;">${user.id}</small></td>
            <td><strong>${escapeHtml(user.name)}</strong></td>
            <td>
                <button class="action-btn edit-btn" onclick="openEditModal('${user.id}', '${escapeHtml(user.name)}')">✏️ Edit</button>
                <button class="action-btn delete-btn" onclick="deleteUser('${user.id}', '${escapeHtml(user.name)}')">🗑️ Delete</button>
            </td>
        </tr>
    `).join('');
}

// Update Users Stats
function updateUsersStats() {
    document.getElementById('totalUsers').textContent = usersData.length;
}

// Filter Users
function filterUsers() {
    const searchTerm = document.getElementById('searchUsers').value.toLowerCase();
    const filtered = usersData.filter(user => 
        user.name.toLowerCase().includes(searchTerm) ||
        user.id.toLowerCase().includes(searchTerm)
    );
    
    const tbody = document.getElementById('usersBody');
    
    if (filtered.length === 0) {
        tbody.innerHTML = '<tr><td colspan="3" class="empty-state"><div>No matching users found</div></td></tr>';
        return;
    }
    
    tbody.innerHTML = filtered.map(user => `
        <tr>
            <td><small style="color: #999;">${user.id}</small></td>
            <td><strong>${escapeHtml(user.name)}</strong></td>
            <td>
                <button class="action-btn edit-btn" onclick="openEditModal('${user.id}', '${escapeHtml(user.name)}')">✏️ Edit</button>
                <button class="action-btn delete-btn" onclick="deleteUser('${user.id}', '${escapeHtml(user.name)}')">🗑️ Delete</button>
            </td>
        </tr>
    `).join('');
}

// ==================== MODAL FUNCTIONS ====================

// Open Edit Modal
function openEditModal(userId, userName) {
    currentEditId = userId;
    // currentEditOldName = userName; // Không dùng nữa
    document.getElementById('editName').value = userName;
    document.getElementById('editModal').style.display = 'block';
}

// Close Modal
function closeModal() {
    document.getElementById('editModal').style.display = 'none';
    currentEditId = null;
    currentEditOldName = null;
}

// Save Edit - Updates both users and all attendance records
function saveEdit() {
    const newName = document.getElementById('editName').value.trim();
    
    if (!newName) {
        alert('Please enter a valid name!');
        return;
    }
    
    // Update user name in users table
    database.ref('users/' + currentEditId).update({
        name: newName
    }).then(() => {
        // FIX: Cập nhật attendance dựa trên ID chứ không dựa trên tên cũ
        return updateAttendanceRecordsById(currentEditId, newName);
    }).then(() => {
        alert('✅ User name updated successfully in both Users and Attendance records!');
        closeModal();
    }).catch((error) => {
        alert('❌ Error updating user: ' + error.message);
    });
}

// FIX: Hàm mới để update dựa trên ID (Chính xác hơn)
function updateAttendanceRecordsById(userId, newName) {
    return database.ref('attendance').once('value').then((snapshot) => {
        const updates = {};
        const data = snapshot.val();
        
        if (data) {
            Object.keys(data).forEach(key => {
                // So sánh ID trong record attendance với userId đang sửa
                if (data[key].id && String(data[key].id) === String(userId)) {
                    updates[`attendance/${key}/name`] = newName;
                }
            });
        }
        
        // Apply all updates at once
        if (Object.keys(updates).length > 0) {
            return database.ref().update(updates);
        }
        
        return Promise.resolve();
    });
}

// Delete User
function deleteUser(userId, userName) {
    if (confirm(`Are you sure you want to delete user "${userName}"?\n\nNote: This will NOT delete their attendance records.`)) {
        database.ref('users/' + userId).remove()
            .then(() => {
                alert('✅ User deleted successfully!');
            })
            .catch((error) => {
                alert('❌ Error deleting user: ' + error.message);
            });
    }
}

// ==================== UTILITY FUNCTIONS ====================

// Escape HTML to prevent XSS
function escapeHtml(text) {
    if (!text) return "";
    const map = {
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#039;'
    };
    return text.toString().replace(/[&<>"']/g, m => map[m]);
}

// Close modal when clicking outside
window.onclick = function(event) {
    const modal = document.getElementById('editModal');
    if (event.target == modal) {
        closeModal();
    }
}

// ==================== INITIALIZATION ====================

// Initialize the application
loadAttendance();
loadUsers();