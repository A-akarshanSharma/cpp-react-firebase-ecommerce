import { Link, useNavigate } from 'react-router-dom'
import { useAuth } from '../context/AuthContext'
import { useCart } from '../context/CartContext'

export default function Navbar() {
  const { user, isAdmin, logout } = useAuth()
  const { itemCount } = useCart()
  const navigate = useNavigate()

  const handleLogout = async () => {
    await logout()
    navigate('/')
  }

  return (
    <header className="site-header">
      <div className="site-header__inner">
        <Link to="/" className="site-header__brand">Studio Thread</Link>

        <nav className="site-nav">
          <Link to="/" className="site-nav__link">Shop</Link>
          {user && <Link to="/orders" className="site-nav__link">Orders</Link>}
          {isAdmin && <Link to="/admin/products" className="site-nav__link">Admin</Link>}
          {user ? (
            <button className="site-nav__link" onClick={handleLogout} style={{ background: 'none', border: 'none', cursor: 'pointer', font: 'inherit', padding: 0 }}>
              Sign out
            </button>
          ) : (
            <Link to="/login" className="site-nav__link">Sign in</Link>
          )}
          <Link to="/cart" className="site-nav__cart">
            Cart
            {itemCount > 0 && <span className="site-nav__badge">{itemCount}</span>}
          </Link>
        </nav>
      </div>
    </header>
  )
}
