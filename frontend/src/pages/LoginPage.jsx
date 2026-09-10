import { useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { useAuth } from '../context/AuthContext'

export default function LoginPage() {
  const { login, signup } = useAuth()
  const navigate = useNavigate()
  const [mode, setMode] = useState('login') // 'login' | 'signup'
  const [email, setEmail] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState(null)
  const [submitting, setSubmitting] = useState(false)

  const handleSubmit = async (e) => {
    e.preventDefault()
    setError(null)
    setSubmitting(true)
    try {
      if (mode === 'login') {
        await login(email, password)
      } else {
        await signup(email, password)
      }
      navigate('/')
    } catch (err) {
      setError(friendlyAuthError(err.code))
    } finally {
      setSubmitting(false)
    }
  }

  return (
    <div className="page-container auth-page">
      <h1>{mode === 'login' ? 'Sign in' : 'Create an account'}</h1>

      <form className="auth-form" onSubmit={handleSubmit}>
        <label htmlFor="email">Email</label>
        <input
          id="email"
          type="email"
          value={email}
          onChange={(e) => setEmail(e.target.value)}
          required
        />

        <label htmlFor="password">Password</label>
        <input
          id="password"
          type="password"
          value={password}
          onChange={(e) => setPassword(e.target.value)}
          required
          minLength={6}
        />

        {error && <p className="error-text" style={{ marginTop: 16 }}>{error}</p>}

        <button className="btn btn--primary btn--full auth-form__submit" disabled={submitting}>
          {submitting ? 'Please wait…' : mode === 'login' ? 'Sign in' : 'Create account'}
        </button>
      </form>

      <div className="auth-toggle">
        {mode === 'login' ? (
          <>Don't have an account? <a href="#" onClick={(e) => { e.preventDefault(); setMode('signup') }}>Create one</a></>
        ) : (
          <>Already have an account? <a href="#" onClick={(e) => { e.preventDefault(); setMode('login') }}>Sign in</a></>
        )}
      </div>
    </div>
  )
}

function friendlyAuthError(code) {
  switch (code) {
    case 'auth/invalid-credential':
    case 'auth/wrong-password':
    case 'auth/user-not-found':
      return 'Incorrect email or password.'
    case 'auth/email-already-in-use':
      return 'An account already exists with that email.'
    case 'auth/weak-password':
      return 'Password should be at least 6 characters.'
    default:
      return 'Something went wrong. Try again.'
  }
}
