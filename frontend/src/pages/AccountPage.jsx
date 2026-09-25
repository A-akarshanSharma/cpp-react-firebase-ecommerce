import { Link, useNavigate } from 'react-router-dom'
import { useAuth } from '../context/AuthContext'
import { useToast } from '../context/ToastContext'
import { Button, Icon, PageHeading, StatusBadge } from '../components/common/UI'
export default function AccountPage() {
  const { profile, role, logout, isAdmin } = useAuth(),
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
    <div className="container section">
      <PageHeading eyebrow="YOUR CORNER OF THE STUDIO" title="Make yourself at home." />
      <div className="account-grid">
        <section className="panel">
          <span className="state-icon">
            <Icon name="user" size={30} />
          </span>
          <h2>Your account</h2>
          <dl className="profile-details">
            <dt>Email address</dt>
            <dd>{profile.email || 'No email provided'}</dd>
            <dt>Account type</dt>
            <dd>
              <StatusBadge status={role} />
            </dd>
          </dl>
          <Button variant="secondary" onClick={signout}>
            Sign out
          </Button>
        </section>
        <div className="account-shortcuts">
          <Link className="panel shortcut" to="/orders">
            <Icon name="box" size={28} />
            <div>
              <h2>Your orders</h2>
              <p>Revisit your selections and see their status.</p>
            </div>
            <Icon name="arrow" />
          </Link>
          <Link className="panel shortcut" to="/cart">
            <Icon name="bag" size={28} />
            <div>
              <h2>Your bag</h2>
              <p>Pick up where you left off.</p>
            </div>
            <Icon name="arrow" />
          </Link>
          {isAdmin && (
            <Link className="panel shortcut" to="/admin">
              <Icon name="grid" />
              <h2>Store dashboard</h2>
              <Icon name="arrow" />
            </Link>
          )}
        </div>
      </div>
    </div>
  )
}
