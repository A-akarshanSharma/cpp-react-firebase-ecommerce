import { useState } from 'react'
import { Link, Navigate, useLocation } from 'react-router-dom'
import { useAuth } from '../context/AuthContext'
import { Button, ErrorState, Icon, Loader } from '../components/common/UI'
const messages = {
  'auth/invalid-credential': 'The email or password is incorrect.',
  'auth/wrong-password': 'The email or password is incorrect.',
  'auth/user-not-found': 'The email or password is incorrect.',
  'auth/email-already-in-use': 'An account already exists with this email. Please sign in.',
  'auth/weak-password': 'Use a password with at least 6 characters.',
  'auth/invalid-email': 'Enter a valid email address.',
  'auth/too-many-requests': 'Too many attempts. Please wait a moment before trying again.',
  'auth/network-request-failed': 'Check your internet connection and try again.',
  'auth/operation-not-allowed': 'Email and password sign-in is not enabled for this store.',
  'auth/user-disabled': 'This account has been disabled. Please contact the store owner.',
}
export default function LoginPage({ register = false }) {
  const {
    authenticated,
    user,
    loading,
    error: profileError,
    refreshProfile,
    logout,
    login,
    signup,
  } = useAuth()
  const location = useLocation(),
    from = location.state?.from
  const destination =
    typeof from === 'string' &&
    from.startsWith('/') &&
    !from.startsWith('//') &&
    !from.includes('\\') &&
    !['/login', '/register'].includes(from)
      ? from
      : '/account'
  const [email, setEmail] = useState(''),
    [password, setPassword] = useState(''),
    [confirm, setConfirm] = useState(''),
    [error, setError] = useState(''),
    [busy, setBusy] = useState(false),
    [visible, setVisible] = useState(false)
  const submit = async (e) => {
    e.preventDefault()
    if (busy) return
    setError('')
    if (register && password !== confirm) {
      setError('Your passwords do not match.')
      return
    }
    setBusy(true)
    try {
      await (register ? signup : login)(email.trim(), password)
    } catch (e) {
      setError(messages[e.code] || e.message || 'Sign-in failed. Please try again.')
    } finally {
      setBusy(false)
    }
  }
  if (loading) return <Loader />
  if (authenticated) return <Navigate to={destination} replace />
  return (
    <div className="container auth-page">
      <div className="auth-story">
        <p className="eyebrow">WELCOME TO THE STUDIO</p>
        <h1>
          Your everyday.
          <br />
          <em>A little better.</em>
        </h1>
        <p>
          A place for thoughtful finds
          <br />
          and things that feel like you.
        </p>
        <span className="auth-flower">✳</span>
        <span className="eyebrow">GOOD THINGS START HERE.</span>
      </div>
      <div className="auth-form-wrap">
        <Icon name="user" size={28} />
        <h2>{register ? 'Make yourself at home.' : 'Good to see you again.'}</h2>
        <p className="muted">
          {register
            ? 'Create an account to start your collection.'
            : 'Sign in to your Studio Thread account.'}
        </p>
        {user && profileError ? (
          <>
            <ErrorState message={profileError} retry={refreshProfile} />
            <Button variant="secondary" onClick={() => logout().catch((e) => setError(e.message))}>
              Sign out and try again
            </Button>
          </>
        ) : (
          <form onSubmit={submit} className="form-stack">
            <label>
              Email address
              <input
                type="email"
                autoComplete="email"
                required
                value={email}
                onChange={(e) => setEmail(e.target.value)}
                placeholder="you@example.com"
              />
            </label>
            <label>
              Password
              <div className="password-field">
                <input
                  aria-label="Password"
                  type={visible ? 'text' : 'password'}
                  autoComplete={register ? 'new-password' : 'current-password'}
                  minLength={register ? 6 : undefined}
                  required
                  value={password}
                  onChange={(e) => setPassword(e.target.value)}
                />
                <button
                  type="button"
                  aria-label={visible ? 'Hide password' : 'Show password'}
                  onClick={() => setVisible(!visible)}
                >
                  {visible ? 'Hide' : 'Show'}
                </button>
              </div>
            </label>
            {register && (
              <>
                <p className="field-help">Use at least 6 characters.</p>
                <label>
                  Confirm password
                  <input
                    type={visible ? 'text' : 'password'}
                    autoComplete="new-password"
                    required
                    value={confirm}
                    onChange={(e) => setConfirm(e.target.value)}
                  />
                </label>
              </>
            )}
            {(error || profileError) && (
              <p className="form-error" role="alert">
                {error || profileError}
              </p>
            )}
            <Button disabled={busy}>
              {busy ? 'Please wait…' : register ? 'Create account' : 'Sign in'}
              <Icon name="arrow" />
            </Button>
          </form>
        )}
        <p className="auth-switch">
          {register ? 'Already part of the studio?' : 'New around here?'}{' '}
          <Link to={register ? '/login' : '/register'} state={{ from: destination }}>
            {register ? 'Sign in' : 'Create an account'}
          </Link>
        </p>
        <p className="field-help">Your session is securely managed by Firebase Authentication.</p>
      </div>
    </div>
  )
}
