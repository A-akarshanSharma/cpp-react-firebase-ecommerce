import { useState } from 'react'
import { Link, NavLink, Outlet, useNavigate } from 'react-router-dom'
import { useAuth } from '../../context/AuthContext'
import { useToast } from '../../context/ToastContext'
import { Button, Icon } from '../../components/common/UI'
export default function AdminLayout() {
  const [open, setOpen] = useState(false),
    { profile, logout } = useAuth(),
    navigate = useNavigate(),
    notify = useToast()
  const signout = async () => {
    try {
      await logout()
      navigate('/')
    } catch (e) {
      notify(e.message, 'error')
    }
  }
  return (
    <div className="admin-shell">
      <aside className={`admin-sidebar ${open ? 'open' : ''}`}>
        <Link to="/" className="brand">
          <span className="brand-mark">st.</span>
          <span>
            STUDIO
            <br />
            THREAD
          </span>
        </Link>
        <p className="eyebrow">STORE MANAGEMENT</p>
        <nav aria-label="Admin navigation">
          {[
            ['', 'grid', 'Overview'],
            ['/products', 'bag', 'Products'],
            ['/orders', 'box', 'Orders'],
            ['/inventory', 'box', 'Inventory'],
            ['/shipping', 'box', 'Shipping'],
            ['/audit', 'shield', 'Audit log'],
            ['/users', 'user', 'Users'],
          ].map(([path, icon, label]) => (
            <NavLink key={path} to={`/admin${path}`} end={!path} onClick={() => setOpen(false)}>
              <Icon name={icon} />
              {label}
            </NavLink>
          ))}
        </nav>
        <div className="sidebar-bottom">
          <Link to="/">
            <Icon name="arrow" /> Back to storefront
          </Link>
          <Button variant="text" onClick={signout}>
            Sign out
          </Button>
        </div>
      </aside>
      <div className="admin-workspace">
        <header className="admin-topbar">
          <button
            className="mobile-toggle"
            onClick={() => setOpen(!open)}
            aria-label="Toggle admin navigation"
            aria-expanded={open}
          >
            <Icon name="menu" />
          </button>
          <span>
            Studio Thread <span className="muted">/ Administration</span>
          </span>
          <div className="admin-identity">
            <span>{profile.email}</span>
            <span className="avatar">{(profile.email || 'A')[0].toUpperCase()}</span>
          </div>
        </header>
        <main id="main-content" className="admin-main">
          <Outlet />
        </main>
        <footer className="admin-footer">Studio Thread · Store management</footer>
      </div>
    </div>
  )
}
