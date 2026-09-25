import { useRef, useState } from 'react'
import { api } from '../../services/api'
import { useResource } from '../../hooks/useResource'
import { useAuth } from '../../context/AuthContext'
import { useToast } from '../../context/ToastContext'
import {
  EmptyState,
  ErrorState,
  Loader,
  Modal,
  PageHeading,
  StatusBadge,
} from '../../components/common/UI'
export default function AdminUsersPage() {
  const { data, loading, error, reload } = useResource(api.listUsers),
    { profile, refreshProfile } = useAuth(),
    notify = useToast()
  const [query, setQuery] = useState(''),
    [change, setChange] = useState(null),
    [busy, setBusy] = useState(false),
    [failure, setFailure] = useState(''),
    lock = useRef(false)
  const update = async () => {
    if (lock.current) return
    lock.current = true
    setBusy(true)
    setFailure('')
    try {
      await api.setUserRole(change.user.uid, change.role)
      notify('User role updated.')
      const self = change.user.uid === profile.uid
      setChange(null)
      if (self) await refreshProfile()
      else await reload()
    } catch (e) {
      setFailure(e.message)
    } finally {
      lock.current = false
      setBusy(false)
    }
  }
  const users = (data || []).filter((u) =>
    `${u.email} ${u.uid} ${u.role}`.toLowerCase().includes(query.toLowerCase()),
  )
  const adminCount = (data || []).filter((u) => u.role === 'admin').length
  return (
    <>
      <PageHeading eyebrow="PEOPLE & PERMISSIONS" title="Users">
        Manage access to your store.
      </PageHeading>
      <div className="panel">
        <div className="table-toolbar">
          <input
            aria-label="Search users"
            type="search"
            placeholder="Search email, user ID, or role…"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
          <span className="muted">{users.length} users</span>
        </div>
        {loading ? (
          <Loader />
        ) : error ? (
          <ErrorState message={error} retry={reload} />
        ) : !users.length ? (
          <EmptyState title="No users found">
            Try a different search, or check back after users sign in.
          </EmptyState>
        ) : (
          <div className="table-scroll">
            <table>
              <thead>
                <tr>
                  <th>Email</th>
                  <th>User ID</th>
                  <th>Role</th>
                  <th>Change role</th>
                </tr>
              </thead>
              <tbody>
                {users.map((u) => (
                  <tr key={u.uid}>
                    <td>
                      {u.email || 'No email provided'}
                      {u.uid === profile.uid && <small className="muted">You</small>}
                    </td>
                    <td className="break-id">{u.uid}</td>
                    <td>
                      <StatusBadge status={u.role} />
                    </td>
                    <td>
                      <select
                        aria-label={`Role for ${u.email || u.uid}`}
                        value={u.role}
                        disabled={busy || (u.role === 'admin' && adminCount === 1)}
                        onChange={(e) => {
                          setChange({ user: u, role: e.target.value })
                          setFailure('')
                        }}
                      >
                        <option value="customer">Customer</option>
                        <option value="admin">Admin</option>
                      </select>
                      {u.role === 'admin' && adminCount === 1 && (
                        <p className="field-help">
                          Last administrator — promote another account first.
                        </p>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
      {change && (
        <Modal
          title="Change account permissions?"
          confirmLabel="Change role"
          onClose={() => setChange(null)}
          onConfirm={update}
          busy={busy}
          danger={change.user.uid === profile.uid}
        >
          <p>
            Change <strong>{change.user.email || change.user.uid}</strong> to{' '}
            <strong>{change.role}</strong>?
          </p>
          <p>
            {change.role === 'admin'
              ? 'This grants access to product, order, and user management.'
              : 'This removes access to all store management tools.'}
          </p>
          {change.user.uid === profile.uid && (
            <p className="form-error">You will lose your own admin access immediately.</p>
          )}
          {failure && (
            <p className="form-error" role="alert">
              {failure}
            </p>
          )}
        </Modal>
      )}
    </>
  )
}
