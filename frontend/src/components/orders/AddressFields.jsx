export const emptyAddress = {
  name: '',
  phone: '',
  line1: '',
  line2: '',
  city: '',
  region: '',
  postalCode: '',
  country: 'IN',
}
export default function AddressFields({ value, onChange, disabled = false }) {
  const fields = [
    ['name', 'Recipient name', 160, 'name'],
    ['phone', 'Phone number', 40, 'tel'],
    ['line1', 'Address line 1', 200, 'address-line1'],
    ['line2', 'Address line 2 (optional)', 200, 'address-line2'],
    ['city', 'City', 100, 'address-level2'],
    ['region', 'State / region', 100, 'address-level1'],
    ['postalCode', 'Postal code', 24, 'postal-code'],
    ['country', 'Country code (e.g. IN)', 2, 'country'],
  ]
  return (
    <fieldset className="address-fields" disabled={disabled}>
      <legend>Delivery address</legend>
      {fields.map(([key, label, maxLength, autoComplete]) => (
        <label key={key}>
          {label}
          <input
            value={value[key] || ''}
            required={key !== 'line2'}
            maxLength={maxLength}
            autoComplete={autoComplete}
            type={key === 'phone' ? 'tel' : 'text'}
            pattern={key === 'country' ? '[A-Z]{2}' : undefined}
            onChange={(e) =>
              onChange({
                ...value,
                [key]: key === 'country' ? e.target.value.toUpperCase() : e.target.value,
              })
            }
          />
        </label>
      ))}
    </fieldset>
  )
}
