import { useEffect, useState } from 'react'
import { Link, NavLink, useLocation, useNavigate } from 'react-router-dom'
import { useAuth } from '../context/AuthContext'
import { useCart } from '../context/CartContext'
import { useCatalog } from '../context/CatalogContext'
import { useToast } from '../context/ToastContext'
import { Icon } from './common/UI'
import { categoryLink } from '../utils/format'
export default function Navbar() {
  const { user, isAdmin, logout } = useAuth(),
    { itemCount } = useCart(),
    { categories } = useCatalog()
  const [open, setOpen] = useState(false),
    [search, setSearch] = useState('')
  const location = useLocation(),
    navigate = useNavigate(),
    notify = useToast()
  useEffect(() => {
    setOpen(false)
    setSearch(new URLSearchParams(location.search).get('q') || '')
  }, [location])
  const signout = async () => {
    try {
      await logout()
      navigate('/')
    } catch (e) {
      notify(e.message, 'error')
    }
  }
  return (
    <>
      <div className="announcement">
        Thoughtfully chosen. Made for your everyday.{' '}
        <span>
          Discover Studio Thread <Icon name="arrow" size={14} />
        </span>
      </div>
      <header className="site-header">
        <div className="header-main container">
          <Link to="/" className="brand" aria-label="Studio Thread home">
            <span className="brand-mark">st.</span>
            <span>
              STUDIO
              <br />
              THREAD<span className="brand-dot">®</span>
            </span>
          </Link>
          <form
            className="header-search"
            role="search"
            onSubmit={(e) => {
              e.preventDefault()
              navigate(`/shop?q=${encodeURIComponent(search)}`)
            }}
          >
            <Icon name="search" />
            <input
              aria-label="Search the store"
              placeholder="Find your next everyday favorite"
              value={search}
              onChange={(e) => setSearch(e.target.value)}
            />
            <button aria-label="Submit search">
              <Icon name="arrow" size={18} />
            </button>
          </form>
          <div className="header-actions">
            <Link className="account-link" to={user ? '/account' : '/login'}>
              <Icon name="user" />
              <span>{user ? 'My account' : 'Sign in'}</span>
            </Link>
            <Link className="bag-link" to="/cart" aria-label={`Shopping bag, ${itemCount} items`}>
              <Icon name="bag" />
              <span className="bag-label">Bag</span>
              <span className="cart-count">{itemCount}</span>
            </Link>
            <button
              className="mobile-toggle"
              aria-label="Toggle navigation"
              aria-expanded={open}
              aria-controls="store-navigation"
              onClick={() => setOpen(!open)}
            >
              <Icon name="menu" />
            </button>
          </div>
        </div>
        <nav
          id="store-navigation"
          className={`store-nav container ${open ? 'open' : ''}`}
          aria-label="Main navigation"
        >
          <div className="nav-primary">
            <NavLink to="/" end>
              Home
            </NavLink>
            <NavLink to="/shop">Shop all</NavLink>
            {categories.slice(0, 4).map((c) => (
              <Link key={c} to={categoryLink(c)}>
                {c}
              </Link>
            ))}
          </div>
          <div className="nav-secondary">
            <NavLink to="/orders">Orders</NavLink>
            {user && <NavLink to="/notifications">Updates</NavLink>}
            {isAdmin && <NavLink to="/admin">Dashboard ↗</NavLink>}
            {user && <button onClick={signout}>Sign out</button>}
            <Link to="/about">Our story</Link>
          </div>
        </nav>
      </header>
    </>
  )
}
