import { NavLink, Outlet, Navigate } from 'react-router-dom'
import { useAuth } from '../../context/AuthContext'

export default function AdminLayout() {
  const { user, isAdmin, loading } = useAuth()

  if (loading) return <div className="page-container"><p className="meta">Loading…</p></div>

  // Not logged in, or logged in but not an admin - don't reveal that this section exists.
  if (!user || !isAdmin) return <Navigate to="/" replace />

  return (
    <div className="admin-shell">
      <aside className="admin-sidebar">
        <div className="admin-sidebar__title">Admin</div>
        <NavLink
          to="/admin/products"
          className={({ isActive }) => `admin-sidebar__link ${isActive ? 'admin-sidebar__link--active' : ''}`}
        >
          Products
        </NavLink>
        <NavLink
          to="/admin/orders"
          className={({ isActive }) => `admin-sidebar__link ${isActive ? 'admin-sidebar__link--active' : ''}`}
        >
          Orders
        </NavLink>
        <NavLink
          to="/admin/users"
          className={({ isActive }) => `admin-sidebar__link ${isActive ? 'admin-sidebar__link--active' : ''}`}
        >
          Users
        </NavLink>
      </aside>
      <main className="admin-main">
        <Outlet />
      </main>
    </div>
  )
}
