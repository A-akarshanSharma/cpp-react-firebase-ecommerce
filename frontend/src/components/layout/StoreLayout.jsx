import { Link, Outlet, useLocation } from 'react-router-dom'
import { useEffect } from 'react'
import Navbar from '../Navbar'
export function RouteEffects() {
  const { pathname } = useLocation()
  useEffect(() => {
    window.scrollTo(0, 0)
    document.title = `${pathname === '/' ? 'Thoughtful everyday finds' : pathname.split('/').filter(Boolean).join(' · ')} | Studio Thread`
  }, [pathname])
  return null
}
export default function StoreLayout() {
  return (
    <>
      <Navbar />
      <main id="main-content">
        <Outlet />
      </main>
      <footer className="site-footer">
        <div className="container footer-grid">
          <div>
            <Link to="/" className="footer-brand">
              Studio Thread<span>®</span>
            </Link>
            <p>
              Good things, thoughtfully chosen.
              <br />
              Find a little more joy in the everyday.
            </p>
          </div>
          <div>
            <h3>Explore</h3>
            <Link to="/shop">Shop all</Link>
            <Link to="/about">Our story</Link>
            <Link to="/account">My account</Link>
          </div>
          <div>
            <h3>Here to help</h3>
            <Link to="/support">Customer service</Link>
            <Link to="/orders">Your orders</Link>
            <Link to="/contact">Contact</Link>
          </div>
          <div>
            <h3>A thoughtful approach</h3>
            <p>
              A considered collection.
              <br />A simpler way to shop.
            </p>
            <Link to="/social">Follow the studio ↗</Link>
          </div>
        </div>
        <div className="container footer-bottom">
          <span>© {new Date().getFullYear()} Studio Thread</span>
          <div>
            <Link to="/privacy">Privacy</Link>
            <Link to="/terms">Terms</Link>
            <span>India · INR ₹</span>
          </div>
        </div>
      </footer>
    </>
  )
}
