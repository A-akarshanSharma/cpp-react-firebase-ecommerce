import { Navigate, Outlet, useLocation } from 'react-router-dom'
import { useAuth } from '../../context/AuthContext'
import { ErrorState, Loader, EmptyState, Button } from './UI'
import { useToast } from '../../context/ToastContext'
export default function ProtectedRoute({ admin = false }) {
  const { user, profile, loading, error, refreshProfile, logout, isAdmin } = useAuth()
  const location = useLocation(),
    notify = useToast()
  if (loading) return <Loader />
  if (!user)
    return <Navigate to="/login" replace state={{ from: location.pathname + location.search }} />
  if (error || !profile)
    return (
      <div className="container section">
        <ErrorState
          message={error || 'Your account could not be verified.'}
          retry={refreshProfile}
        />
        <Button
          variant="secondary"
          onClick={() => logout().catch((e) => notify(e.message, 'error'))}
        >
          Sign out
        </Button>
      </div>
    )
  if (admin && !isAdmin)
    return (
      <div className="container section">
        <EmptyState title="This area is for store administrators" to="/" action="Back to the store">
          Your account does not have access to store management.
        </EmptyState>
      </div>
    )
  return <Outlet />
}
