import { useEffect, useState } from 'react'
import { api } from '../../services/api'
import { useAuth } from '../../context/AuthContext'

export default function AdminUsersPage() {
  const { user } = useAuth()
  const [users, setUsers] = useState([])
  const [loading, setLoading] = useState(true)
  const [updatingUid, setUpdatingUid] = useState(null)

  const load = () => {
    setLoading(true)
    api.listUsers().then(setUsers).finally(() => setLoading(false))
  }

  useEffect(load, [])

  const handleRoleChange = async (uid, role) => {
    if (uid === user?.uid && role !== 'admin') {
      const confirmed = window.confirm("This removes your own admin access. You'll be signed out of the admin panel immediately. Continue?")
      if (!confirmed) return
    }
    setUpdatingUid(uid)
    try {
      await api.setUserRole(uid, role)
      setUsers((prev) => prev.map((u) => (u.uid === uid ? { ...u, role } : u)))
    } finally {
      setUpdatingUid(null)
    }
  }

  return (
    <div>
      <div className="admin-header">
        <h2>Users</h2>
      </div>

      {loading ? (
        <p className="meta">Loading…</p>
      ) : (
        <table className="admin-table">
          <thead>
            <tr>
              <th>Email</th>
              <th>Role</th>
              <th>Change role</th>
            </tr>
          </thead>
          <tbody>
            {users.map((u) => (
              <tr key={u.uid}>
                <td>{u.email || <span className="meta">—</span>}</td>
                <td>
                  <span className={`admin-badge ${u.role === 'admin' ? 'admin-badge--admin' : ''}`}>{u.role}</span>
                </td>
                <td>
                  <select
                    className="admin-select"
                    value={u.role}
                    disabled={updatingUid === u.uid}
                    onChange={(e) => handleRoleChange(u.uid, e.target.value)}
                  >
                    <option value="customer">customer</option>
                    <option value="admin">admin</option>
                  </select>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </div>
  )
}
